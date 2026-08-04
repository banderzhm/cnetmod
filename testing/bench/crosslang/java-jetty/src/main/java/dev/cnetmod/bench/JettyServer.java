package dev.cnetmod.bench;

import java.nio.file.Files;
import java.nio.file.Path;
import org.eclipse.jetty.alpn.server.ALPNServerConnectionFactory;
import org.eclipse.jetty.http.HttpHeader;
import org.eclipse.jetty.http2.server.HTTP2CServerConnectionFactory;
import org.eclipse.jetty.http2.server.HTTP2ServerConnectionFactory;
import org.eclipse.jetty.http3.server.HTTP3ServerConnectionFactory;
import org.eclipse.jetty.http3.server.HTTP3ServerQuicConfiguration;
import org.eclipse.jetty.io.Content;
import org.eclipse.jetty.quic.quiche.server.QuicheServerConnector;
import org.eclipse.jetty.quic.quiche.server.QuicheServerQuicConfiguration;
import org.eclipse.jetty.server.Handler;
import org.eclipse.jetty.server.HttpConfiguration;
import org.eclipse.jetty.server.HttpConnectionFactory;
import org.eclipse.jetty.server.Request;
import org.eclipse.jetty.server.Response;
import org.eclipse.jetty.server.Server;
import org.eclipse.jetty.server.ServerConnector;
import org.eclipse.jetty.server.SslConnectionFactory;
import org.eclipse.jetty.util.Callback;
import org.eclipse.jetty.util.ssl.SslContextFactory;
import org.eclipse.jetty.util.thread.QueuedThreadPool;

public final class JettyServer {
    private JettyServer() {}

    public static void main(String[] arguments) throws Exception {
        int port = 18087;
        String mode = "http1";
        Path keyStore = null;
        String password = "changeit";
        Path pemDirectory = Path.of("target/quiche-pem");
        for (int index = 0; index < arguments.length; ++index) {
            switch (arguments[index]) {
                case "--port" -> port = Integer.parseInt(arguments[++index]);
                case "--mode" -> mode = arguments[++index];
                case "--keystore" -> keyStore = Path.of(arguments[++index]);
                case "--password" -> password = arguments[++index];
                case "--pem-dir" -> pemDirectory = Path.of(arguments[++index]);
                default -> throw new IllegalArgumentException(arguments[index]);
            }
        }

        var threads = new QueuedThreadPool(256, 16);
        threads.setName("jetty-bench");
        var server = new Server(threads);
        var httpConfiguration = new HttpConfiguration();
        var ssl = keyStore == null ? null : sslContext(keyStore, password);

        switch (mode) {
            case "http1" -> {
                var connector = ssl == null
                        ? new ServerConnector(server, new HttpConnectionFactory(httpConfiguration))
                        : new ServerConnector(
                                server,
                                new SslConnectionFactory(ssl, "http/1.1"),
                                new HttpConnectionFactory(httpConfiguration));
                connector.setHost("127.0.0.1");
                connector.setPort(port);
                server.addConnector(connector);
            }
            case "http2" -> {
                ServerConnector connector;
                if (ssl == null) {
                    connector = new ServerConnector(
                            server, new HTTP2CServerConnectionFactory(httpConfiguration));
                } else {
                    var alpn = new ALPNServerConnectionFactory();
                    alpn.setDefaultProtocol("h2");
                    connector = new ServerConnector(
                            server,
                            new SslConnectionFactory(ssl, alpn.getProtocol()),
                            alpn,
                            new HTTP2ServerConnectionFactory(httpConfiguration));
                }
                connector.setHost("127.0.0.1");
                connector.setPort(port);
                server.addConnector(connector);
            }
            case "http3" -> {
                if (ssl == null) {
                    throw new IllegalArgumentException("HTTP/3 requires --keystore");
                }
                Files.createDirectories(pemDirectory);
                var quic = HTTP3ServerQuicConfiguration.configure(
                        new QuicheServerQuicConfiguration(pemDirectory));
                var connector = new QuicheServerConnector(
                        server, ssl, quic, new HTTP3ServerConnectionFactory(httpConfiguration));
                connector.setPort(port);
                server.addConnector(connector);
            }
            default -> throw new IllegalArgumentException("unknown mode: " + mode);
        }

        server.setHandler(new HelloHandler());
        server.start();
        System.out.printf("ready %d %s%n", port, mode);
        server.join();
    }

    private static SslContextFactory.Server sslContext(Path keyStore, String password) {
        var result = new SslContextFactory.Server();
        result.setKeyStorePath(keyStore.toString());
        result.setKeyStorePassword(password);
        result.setKeyManagerPassword(password);
        result.setIncludeProtocols("TLSv1.3");
        return result;
    }

    private static final class HelloHandler extends Handler.Abstract {
        @Override
        public boolean handle(Request request, Response response, Callback callback) {
            response.setStatus(200);
            response.getHeaders().put(HttpHeader.CONTENT_TYPE, "text/plain");
            Content.Sink.write(response, true, "Hello, World!", callback);
            return true;
        }
    }
}
