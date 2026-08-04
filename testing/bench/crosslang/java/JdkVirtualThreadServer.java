import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpHandler;
import com.sun.net.httpserver.HttpServer;
import com.sun.net.httpserver.HttpsConfigurator;
import com.sun.net.httpserver.HttpsServer;
import java.io.IOException;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.KeyStore;
import java.util.concurrent.Executors;
import javax.net.ssl.KeyManagerFactory;
import javax.net.ssl.SSLContext;

public final class JdkVirtualThreadServer {
    private static final byte[] BODY = "Hello, World!".getBytes();

    private static SSLContext sslContext(Path storePath, String password) throws Exception {
        var store = KeyStore.getInstance("PKCS12");
        try (var input = Files.newInputStream(storePath)) {
            store.load(input, password.toCharArray());
        }
        var managers = KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm());
        managers.init(store, password.toCharArray());
        var context = SSLContext.getInstance("TLSv1.3");
        context.init(managers.getKeyManagers(), null, null);
        return context;
    }

    public static void main(String[] arguments) throws Exception {
        int port = 18083;
        Path keyStore = null;
        String password = "changeit";
        for (int index = 0; index < arguments.length; ++index) {
            switch (arguments[index]) {
                case "--port" -> port = Integer.parseInt(arguments[++index]);
                case "--keystore" -> keyStore = Path.of(arguments[++index]);
                case "--password" -> password = arguments[++index];
                default -> throw new IllegalArgumentException(arguments[index]);
            }
        }

        HttpServer server;
        if (keyStore != null) {
            var https = HttpsServer.create(new InetSocketAddress("127.0.0.1", port), 1024);
            https.setHttpsConfigurator(new HttpsConfigurator(sslContext(keyStore, password)));
            server = https;
        } else {
            server = HttpServer.create(new InetSocketAddress("127.0.0.1", port), 1024);
        }
        server.createContext("/hello", new HelloHandler());
        server.setExecutor(Executors.newVirtualThreadPerTaskExecutor());
        server.start();
        System.out.printf("ready %d%n", port);
        Thread.currentThread().join();
    }

    private static final class HelloHandler implements HttpHandler {
        @Override
        public void handle(HttpExchange exchange) throws IOException {
            exchange.getResponseHeaders().set("Content-Type", "text/plain");
            exchange.sendResponseHeaders(200, BODY.length);
            try (OutputStream output = exchange.getResponseBody()) {
                output.write(BODY);
            }
        }
    }
}
