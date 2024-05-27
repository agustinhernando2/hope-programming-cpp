
#include <server.hpp>

using namespace std::chrono_literals;

int main() {
    // start_db();

    std::signal(SIGTERM, signal_handler);  // SIGTERM

    std::vector<std::jthread> serverThreads;    
    // Start servers for IPv4
    std::cout << "Server IPv4 TCP listening: " << localhost_ipv4 << ":" << tcp4_port << std::endl;
    serverThreads.emplace_back([] { run_server(localhost_ipv4, tcp4_port, TCP); });

    // Start servers for IPv6
    std::cout << "Server IPv6 TCP listening: " << localhost_ipv6 << ":" << tcp6_port << std::endl;
    serverThreads.emplace_back([] { run_server(localhost_ipv6, tcp6_port, TCP); });

    // Start HTTP server
    serverThreads.emplace_back([] { start_http_server(); });

    // Start Emergency Module
    serverThreads.emplace_back([] { run_emergency_module(); });
    // Start Alert Module
    serverThreads.emplace_back([] { run_alert_module(); });
    // Start Listener alert module
    serverThreads.emplace_back([] { alert_listener(); });

    // Wait for all threads to finish
    for (auto& thread : serverThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    return 0;
}

int run_server(std::string address, std::string port, int protocol){
    std::unique_ptr<IConnection> server = createConnection(address, port, true, protocol);
    if (server->bind()) {
        while (true) {
            // Accept connection
            int clientFd = server->connect();

            if (clientFd < 0) {
                std::cerr << "Accept error." << std::endl;
                continue;
            }
            std::cout << "Connection accepted. Waiting for messages from the client..." << std::endl;

            // Manage the client in a new thread
            std::jthread clientThread(handle_client, server.get(), clientFd); 
            clientThread.detach(); // free the thread resources
        }
    } else {
        return 1;
    }

    return 0;
}

void handle_client(IConnection* server, int clientFd) {

    std::signal(SIGINT, signal_handler);   // SIGINT

    // Manage the client in a new thread
    std::jthread emergency_listener(run_emergency_listener, clientFd); 
    emergency_listener.detach(); // free the thread resources

    // Internal client to access the HTTP server
    httplib::Client icli(localhost_ipv4, http_port);

    std::string message;
    std::string status;
    try {
        while (true) {
            if (signal_end_conn)
            {
                end_conn(clientFd);
            }
            // Receive message from client
            message = server->receiveFrom(clientFd);
            
            std::cout << "PID: " << getpid() << std::endl; 
            std::cout << "Message received: " << message << std::endl;
            std::this_thread::sleep_for(500ms);

            httplib::Result res;
            switch (get_command(message))
            {
            case 1:
                std::cout << "Getting supplies..." << std::endl;
                res = icli.Get("/supplies");
                status = res->status;
                message = res->body;
                if(message.empty() || status == "404"){
                    message = "No supplies found";
                }
                server->sendto(message,clientFd);
                break;
            case 2:
                std::cout << "Setting supplies..." << std::endl;
                res = icli.Post("/setsupplies",message, "application/json");
                status = res->status;
                message = res->body;
                if(message.empty() || status == "404"){
                    message = "No supplies found";
                }
                server->sendto(message,clientFd);
                break;

            case 3:
                //CANNY EDGE FILTER
                std::cout << "Canny edge filter ..." << std::endl;
                std::this_thread::sleep_for(1s);
                break;
            case 4:
                end_conn(clientFd);
                break;
            default:
                fprintf(stdout, "Command error\n");
                break;
            };

                
        }
    } catch (const std::runtime_error& e) {
        std::cerr << "Error: " << e.what() << std::endl; 
    }
    
    close(clientFd);
}

void alert_listener() {
    mess_t send_buffer;

    while (true) {
        if (msgrcv(msg_id, &send_buffer, sizeof(send_buffer), 1, 0) == -1) {
            // print errno
            perror("msgrcv error");
            std::this_thread::sleep_for(1s);
            continue;
        }

        std::cout << "Alert received: " << send_buffer.message << std::endl;

        try {
            // Open db
            semaphore.acquire();
            std::unique_ptr<RocksDbWrapper> db = std::make_unique<RocksDbWrapper>("data/database.db");

            // Parse alert received
            nlohmann::json alert_received = nlohmann::json::parse(send_buffer.message);

            // Get alert location
            std::string location = alert_received["location"].get<std::string>();
            std::string alerts;

            // Get alerts from db
            db->get(K_ALERTS, alerts);

            // check if alerts is empty
            if (alerts.empty()) {
                alerts = "{}";
            }

            nlohmann::json alerts_in_db = nlohmann::json::parse(alerts);

            alerts_in_db[location] = alerts_in_db[location].get<int>() + 1;

            // update
            db->put(K_ALERTS, alerts_in_db.dump());
        } catch (const std::exception& e) {
            std::cerr << "Error processing alert: " << e.what() << std::endl;
        }
        semaphore.release();
    }
}

void run_emergency_listener(int clientFd) {
    mess_t send_buffer;
    while (true) {
        if (msgrcv(msg_id, &send_buffer, sizeof(send_buffer), 2, 0) == -1) {
            // print errno
            perror("msgrcv error");
            std::this_thread::sleep_for(1s);
            continue;
        }

        std::cout << "Emergency received: " << send_buffer.message << std::endl;

        try {
            // Open db
            semaphore.acquire();
            std::unique_ptr<RocksDbWrapper> db = std::make_unique<RocksDbWrapper>("data/database.db");

            // Parse alert received
            nlohmann::json emergency_received = nlohmann::json::parse(send_buffer.message);

            // Get alert location
            std::string last_keepalived = emergency_received[LAST_KEEP_ALIVED].get<std::string>();
            std::string last_event = emergency_received[LAST_EVENT].get<std::string>();

            // Get alerts from db
            std::string emergency;
            db->get(K_EMERGENCY, emergency);

            // check if alerts is empty
            if (emergency.empty()) {
                emergency = "{}";
            }

            nlohmann::json emergency_in_db = nlohmann::json::parse(emergency);

            emergency_in_db[LAST_KEEP_ALIVED] = last_keepalived;
            emergency_in_db[LAST_EVENT] = last_event;

            // update
            db->put(K_EMERGENCY, emergency_in_db.dump());
            // End connection and exit
        } catch (const std::exception& e) {
            std::cerr << "Error processing emergency: " << e.what() << std::endl;
        }
        semaphore.release();
        end_conn(clientFd);
    }
}

// Datos JSON
const std::string json_data = R"(
{
    "alerts": {
        "NORTH": 74,
        "EAST": 70,
        "WEST": 70,
        "SOUTH": 65
    },
    "supplies": {
        "food": {
            "vegetables": 200,
            "fruits": 200,
            "grains": 200,
            "meat": 200
        },
        "medicine": {
            "antibiotics": 1,
            "analgesics": 100,
            "antipyretics": 100,
            "antihistamines": 100
        }
    },
    "emergency": {
        "last_keepalived": "Sun May 12 22:18:48",
        "last_event": "Sun May 12 22:18:48, Server failure. Emergency notification sent to all connected clients."
    }
}
)";

void start_db() {
    std::unique_ptr<RocksDbWrapper> db = std::make_unique<RocksDbWrapper>("data/database.db");

    try
    {
        // JSON parser
        nlohmann::json data = nlohmann::json::parse(json_data);

        // Insert
        db->put(K_ALERTS, data[K_ALERTS].dump());
        db->put(SUPPLIES_KEY, data[SUPPLIES_KEY].dump());
        db->put("emergency", data["emergency"].dump());

        std::cout << "Database inicialized." << std::endl;
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }
    
}

int get_command(std::string message){

    nlohmann::json j = nlohmann::json::parse(message);
    std::string command = j["command"];
    if(command == option1.command){
        return 1;
    }else if(command == option2.command){
        return 2;
    }else if(command == option3.command){
        return 3;
    }else if(command == option4.command){
        return 4;
    }else{
        return 0;
    }
}

void end_conn(int fd)
{
    std::cout << "Ending connection..." << std::endl;
    std::cout << "Sending end connection message..." << std::endl;
    nlohmann::json j;
    j["message"] = "Connection ended";
    j["command"] = "end";
    std::string message = j.dump();
    // Send end connection message
    send(fd, message.c_str(), message.size(), 0);
    
    std::this_thread::sleep_for(2s);
    exit(EXIT_SUCCESS);
}
void start_http_server() {
    httplib::Server srv;

    srv.Get("/hi", [](const httplib::Request &, httplib::Response &res) {
        std::cout << "GET /hi" << std::endl;
        res.set_content("Hello World!", "text/plain");
    });

    srv.Get("/supplies", [](const httplib::Request &, httplib::Response &res) {
        std::cout << "GET /supplies" << std::endl;

        std::string supplies = get_supplies();

        if (!supplies.empty()) {
            res.set_content(supplies, "application/json");
        } else {
            res.status = 404;
            res.set_content("File not found", "text/plain");
        }
    });

    srv.Get("/database", [](const httplib::Request &, httplib::Response &res) {
        std::cout << "GET /database" << std::endl;

        std::string result;
        std::unique_ptr<RocksDbWrapper> db = std::make_unique<RocksDbWrapper>("data/database.db");
        nlohmann::json j;

        // Insert
        db->get(K_ALERTS, result);
        j[K_ALERTS] = nlohmann::json::parse(result);

        db->get(SUPPLIES_KEY, result);
        j[SUPPLIES_KEY] = nlohmann::json::parse(result);
        
        db->get("emergency", result);
        j["emergency"] = nlohmann::json::parse(result);

        result = j.dump(4);

        if (!result.empty()) {
            res.set_content(result, "application/json");
        } else {
            res.status = 404;
            res.set_content("File not found", "text/plain");
        }
    });

    srv.Get("/alert", [](const httplib::Request &, httplib::Response &res) {

        std::cout << "GET /alert" << std::endl;
             
        std::string alerts;
        std::unique_ptr<RocksDbWrapper> db = std::make_unique<RocksDbWrapper>("data/database.db");
        nlohmann::json j;

        // Get alerts
        db->get(K_ALERTS, alerts);
        j[K_ALERTS] = nlohmann::json::parse(alerts);
        // Format the json
        alerts = j[K_ALERTS].dump(4);

        if (1) {
            res.set_content(alerts, "application/json");
        } else {
            res.status = 404;
            res.set_content("File not found", "text/plain");
        }
    });

    srv.Post("/setsupplies", [](const httplib::Request &req, httplib::Response &res) {
        std::cout << "POST /setsupplies" << std::endl;

        nlohmann::json j = nlohmann::json::parse(req.body);
        if(j.empty()){
            res.status = 404;
            res.set_content("File not found", "text/plain");
        }else{
            semaphore.acquire();
            set_supply(req.body);
            semaphore.release();
            std::string supplie_added = "Supply added\n...\n" + j.dump(4);
            res.set_content(supplie_added, "application/json");
        }
    });

    // listen all interfaces
    srv.listen(localhost_ipv4, http_port);
}

void signal_handler(int signal) {
    std::cout << "Signal " << signal << " received" << std::endl;
    switch (signal)
    {
    case SIGTSTP:
        exit(EXIT_SUCCESS);
        break;
    case SIGINT:
        signal_end_conn = 1;
        break;
    default:
        break;
    }
}
