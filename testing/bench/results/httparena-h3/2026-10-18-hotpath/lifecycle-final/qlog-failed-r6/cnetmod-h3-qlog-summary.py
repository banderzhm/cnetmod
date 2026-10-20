#!/usr/bin/env python3
"""Offline ngtcp2 JSON-SEQ qlog ranges. No network/subprocess/service actions."""
import argparse
import json
from pathlib import Path


def number(v):
    return int(v, 0) if isinstance(v, str) and v.startswith('0x') else int(v)


def merged(ranges):
    result=[]
    for start,end in sorted(ranges):
        if result and start<=result[-1][1]: result[-1][1]=max(result[-1][1],end)
        else: result.append([start,end])
    return result


class ParseLimit(ValueError): pass

# Whole-run object budgets; bounded independently of packet/retransmission count.
def budget(): return dict(streams=0,intervals=0,metadata=0,events=0)
def consume(counts,key,limit):
    counts[key]+=1
    if counts[key]>limit:raise ParseLimit(key+' diagnostic budget exceeded')


def analyze(path,counts=None):
    counts=budget() if counts is None else counts
    streams={}; cids=set(); event_count=0; packet_count=0; errors=[]; close=[]; credit=[]; parameters=[]; trace=False
    def state(sid):
        if sid not in streams:
            consume(counts,'streams',250000)
            streams[sid]=dict(request_seen=False,sent=[],sent_fin_sizes=set(),received=[],fin_sizes=set(),reset=[],stop=[],last_event=None)
        return streams[sid]
    def interval(s,key,start,end):
        previous=len(s[key]);s[key]=merged(s[key]+[[start,end]])
        counts['intervals']+=len(s[key])-previous
        if len(s[key])>4096 or counts['intervals']>600000:raise ParseLimit('interval diagnostic budget exceeded')
    def metadata():consume(counts,'metadata',200000)
    def identifiers(value):
        if isinstance(value,dict):
            for key,item in value.items():
                if (key=='group_id' or 'connection_id' in key or key in ('dcid','scid')) and isinstance(item,str): 
                    if len(item)>40:raise ValueError('CID longer than20bytes')
                    if item:cids.add(item.lower())
                    if len(cids)>8192:raise ParseLimit('CID diagnostic budget exceeded')
                identifiers(item)
        elif isinstance(value,list):
            for item in value: identifiers(item)
    # ngtcp2 emits one complete JSON record per line prefixed by ASCII RS.
    with path.open('rb') as file:
        index=0
        while True:
            line=file.readline(1024*1024+1)
            if not line:break
            index+=1
            if len(line)>1024*1024:
                errors.append(dict(line=index,error='record exceeds1MiB')); break
            if not line.strip(): continue
            try:
                event=json.loads(line.lstrip(b'\x1e'))
                if not isinstance(event,dict): raise ValueError('non-object record')
                identifiers(event)
                if 'qlog_version' in event:
                    trace=event.get('qlog_version')=='0.3' and event.get('qlog_format')=='JSON-SEQ' and event.get('trace',{}).get('vantage_point',{}).get('type')=='client'
                    if not trace: errors.append(dict(line=index,error='unsupported header/vantage'))
                    continue
                event_count+=1
                consume(counts,'events',16000000)
                name=event.get('name')
                if name=='transport:parameters_set':
                    data=event.get('data',{})
                    metadata()
                    parameters.append(dict(time=event.get('time'),owner=data.get('owner'),
                        limits={k:v for k,v in data.items() if k.startswith('initial_max_stream') or k=='initial_max_data'}))
                if name not in ('transport:packet_received','transport:packet_sent'): continue
                packet_count+=1
                data=event.get('data',{})
                frames=data.get('frames',[])
                if not isinstance(frames,list): raise ValueError('frames not list')
                received=name.endswith('received')
                for frame in frames:
                    kind=frame.get('frame_type')
                    if (received and kind=='max_streams') or (not received and kind=='streams_blocked'):
                        key='maximum' if kind=='max_streams' else 'limit'
                        metadata()
                        credit.append(dict(kind=kind,time=event.get('time'),stream_type=frame.get('stream_type'),
                            value=number(frame[key]),packet_number=data.get('header',{}).get('packet_number')))
                    if kind=='connection_close':
                        metadata()
                        close.append(dict(direction='received' if received else 'sent',time=event.get('time'),
                            frame={key:(str(value)[:512] if isinstance(value,str) else value) for key,value in frame.items() if key in ('frame_type','error_space','error_code','raw_error_code','reason','trigger_frame_type')}))
                    if kind not in ('stream','reset_stream','stop_sending'): continue
                    sid=number(frame['stream_id'])
                    if sid%4!=0: continue # request streams are client-initiated bidi
                    s=state(sid); s['last_event']=event.get('time')
                    if kind=='stream':
                        start=number(frame.get('offset',0)); length=number(frame['length'])
                        if min(start,length)<0: raise ValueError('negative stream interval')
                        if received:
                            interval(s,'received',start,start+length)
                            if frame.get('fin',False):
                                s['fin_sizes'].add(start+length)
                                if len(s['fin_sizes'])>2:raise ParseLimit('conflicting FIN diagnostic budget exceeded')
                        else:
                            s['request_seen']=True;interval(s,'sent',start,start+length)
                            if frame.get('fin',False):
                                s['sent_fin_sizes'].add(start+length)
                                if len(s['sent_fin_sizes'])>2:raise ParseLimit('conflicting request FIN diagnostic budget exceeded')
                    else:
                        metadata()
                        s['reset' if kind=='reset_stream' else 'stop'].append(dict(direction='received' if received else 'sent',
                            frame={k:v for k,v in frame.items() if k in ('frame_type','stream_id','error_code','final_size')},time=event.get('time')))
            except (ValueError,TypeError,KeyError,AttributeError,RecursionError) as e:
                errors.append(dict(line=index,error=str(e)))
                # One malformed/capped record invalidates this file; raw source remains retained.
                break
    rows=[]
    for sid,s in sorted(streams.items()):
        sent_ranges=merged(s.pop('sent'));sent_finals=sorted(s.pop('sent_fin_sizes'))
        sent_end=sent_finals[0] if len(sent_finals)==1 else None
        sent_complete=sent_end is not None and ((sent_ranges==[[0,sent_end]]) or (sent_end==0 and not sent_ranges))
        ranges=merged(s.pop('received')); finals=sorted(s.pop('fin_sizes')); end=finals[0] if len(finals)==1 else None
        gaps=[]; cursor=0
        for start,stop in ranges:
            if start>cursor: gaps.append([cursor,start])
            cursor=max(cursor,stop)
        if end is not None and cursor<end: gaps.append([cursor,end])
        valid_fin=end is not None and len(finals)==1 and cursor==end
        complete=valid_fin and not gaps and not s['reset']
        rows.append(dict(stream_id=sid,request_sent_ranges=sent_ranges,request_fin_sizes=sent_finals,request_transport_complete=sent_complete,received_ranges=ranges,fin_sizes=finals,missing_known_ranges=gaps,
                         tail_end_unknown=end is None,received_end=cursor,transport_complete=complete,**s))
    requests=[r for r in rows if r['request_seen']]
    return dict(file=str(path),bytes=path.stat().st_size,identifiers=sorted(cids),trace_header=trace,
                event_count=event_count,packet_events=packet_count,parse_errors=errors,
                diagnostic_complete=trace and packet_count>0 and not errors,
                credit_timeline=credit,transport_parameters=parameters,
                received_max_streams_bidi=[e for e in credit if e['kind']=='max_streams' and e['stream_type']=='bidirectional'],
                sent_streams_blocked_bidi=[e for e in credit if e['kind']=='streams_blocked' and e['stream_type']=='bidirectional'],
                request_streams=len(requests),request_max_stream_id=max((r['stream_id'] for r in requests),default=None),
                request_highest_opened_ordinal=max((r['stream_id']//4+1 for r in requests),default=0),
                requests_with_fin=sum(r['request_transport_complete'] for r in requests),transport_complete=sum(r['transport_complete'] for r in requests),
                incomplete=[r for r in requests if not r['transport_complete']],all_streams=rows,connection_close=close)


def summarize(directory):
    files=sorted(directory.glob('client.*.sqlog'))
    if len(files)>64:raise ParseLimit('more than64 connection qlogs')
    if any(path.stat().st_size>64*1024**2 for path in files):raise ParseLimit('qlog exceeds64MiB')
    counts=budget()
    connections=[analyze(path,counts) for path in files]
    return dict(scope='client transport ranges and FIN; not HTTP body/status verification or RPS ranking',
                diagnostic_complete=bool(files) and all(x['diagnostic_complete'] for x in connections),object_counts=counts,connections=connections)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('directory',type=Path);parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args(); result=summarize(args.directory)
    args.out.write_text(json.dumps(result,indent=2));print(args.out)
    if not result['diagnostic_complete']: raise SystemExit(2)
