export module cnetmod.protocol.raft:transport;

import std;
import :types;

namespace cnetmod::raft
{
    export class raft_transport
    {
    public:
        virtual ~raft_transport() = default;

        virtual void send_pre_vote(const node_id& peer,
                                   const request_vote_request& request) = 0;
        virtual void send_request_vote(const node_id& peer,
                                       const request_vote_request& request) = 0;
        virtual void send_request_vote_response(const node_id& peer,
                                                const request_vote_response& response) = 0;
        virtual void send_timeout_now(const node_id& peer,
                                      const timeout_now_request& request) = 0;
        virtual void send_append_entries(const node_id& peer,
                                         const append_entries_request& request) = 0;
        virtual void send_append_entries_response(const node_id& peer,
                                                  const append_entries_response& response) = 0;
        virtual void send_install_snapshot(const node_id& peer,
                                           const install_snapshot_request& request) = 0;
        virtual void send_install_snapshot_response(const node_id& peer,
                                                    const install_snapshot_response& response) = 0;
    };

    export struct outbound_message
    {
        enum class kind
        {
            pre_vote,
            request_vote,
            request_vote_response,
            timeout_now,
            append_entries,
            append_entries_response,
            install_snapshot,
            install_snapshot_response,
        };

        node_id peer;
        kind type = kind::append_entries;
        request_vote_request vote;
        request_vote_response vote_response;
        timeout_now_request timeout_now;
        append_entries_request append;
        append_entries_response append_response;
        install_snapshot_request snapshot;
        install_snapshot_response snapshot_response;
    };
} // namespace cnetmod::raft