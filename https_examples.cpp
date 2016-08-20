#include "server_https.hpp"
#include "client_https.hpp"

//Added for the json-example
#define BOOST_SPIRIT_THREADSAFE
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

//Added for the default_resource example
#include <fstream>
#include <boost/filesystem.hpp>
#include <vector>
#include <algorithm>

using namespace std;
//Added for the json-example:
using namespace boost::property_tree;

typedef SimpleWeb::Server<SimpleWeb::HTTPS> HttpsServer;
typedef SimpleWeb::Client<SimpleWeb::HTTPS> HttpsClient;

//Added for the default_resource example
void default_resource_send(const HttpsServer &server, shared_ptr<HttpsServer::Response> response,
                           shared_ptr<ifstream> ifs, shared_ptr<vector<char> > buffer);

// MDM blast it out there and see what happens at https://www.ssllabs.com/ssltest/
// We'll have to take down apache and run this as root, let's see what we get.
const int cnPort = 443;
const string cstr_server_url("bitpost.com:443");

class Controller
{
public:
    Controller(boost::asio::io_service& ios) : ios_(ios), timer_(ios) { start_timer(); }

    void start_timer()
    {
        timer_.expires_from_now(boost::posix_time::milliseconds(50));
        timer_.async_wait(boost::bind(&Controller::main_loop,this,boost::asio::placeholders::error));
    }

    void main_loop(const boost::system::error_code& error)
    {
        // MANDATORY, boost will make unsolicited calls with error set.
        if (error) return;


        // Do any controller-like things, including handling timer events,
        // handling any incoming data from https server, exiting, etc.
        cout << "Timer fired..." << std::endl;


        // Prime the next loop.
        timer_.expires_from_now(boost::posix_time::milliseconds(300));
        timer_.async_wait(boost::bind(&Controller::main_loop,this,boost::asio::placeholders::error));
    }

    boost::asio::io_service& ios_;
    boost::asio::deadline_timer timer_;
};


int main() {
    //HTTPS-server at port 8080 using 1 thread
    //Unless you do more heavy non-threaded processing in the resources,
    //1 thread is usually faster than several threads

    boost::asio::io_service ios;
    HttpsServer server(
        ios,
        cnPort,
        1,

        // MDM let's use the full chain, which i downloaded in chrome while visiting bitpost.com
        "/home/m/development/config/StartCom/bitpost.com/2016-/bitpost_chain.crt",
        "/home/m/development/config/StartCom/bitpost.com/2016-/bitpost_5.key"
    );
    // -----------------------

    //Add resources using path-regex and method-string, and an anonymous function
    //POST-example for the path /string, responds the posted string
    server.resource["^/string$"]["POST"]=[](shared_ptr<HttpsServer::Response> response, shared_ptr<HttpsServer::Request> request) {
        //Retrieve string:
        auto content=request->content.string();
        //request->content.string() is a convenience function for:
        //stringstream ss;
        //ss << request->content.rdbuf();
        //string content=ss.str();
        
        *response << "HTTP/1.1 200 OK\r\nContent-Length: " << content.length() << "\r\n\r\n" << content;
    };
    
    //POST-example for the path /json, responds firstName+" "+lastName from the posted json
    //Responds with an appropriate error message if the posted json is not valid, or if firstName or lastName is missing
    //Example posted json:
    //{
    //  "firstName": "John",
    //  "lastName": "Smith",
    //  "age": 25
    //}
    server.resource["^/json$"]["POST"]=[](shared_ptr<HttpsServer::Response> response, shared_ptr<HttpsServer::Request> request) {
        try {
            ptree pt;
            read_json(request->content, pt);

            string name=pt.get<string>("firstName")+" "+pt.get<string>("lastName");
            
            *response << "HTTP/1.1 200 OK\r\nContent-Length: " << name.length() << "\r\n\r\n" << name;
        }
        catch(exception& e) {
            *response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n" << e.what();
        }
    };
    
    //GET-example for the path /info
    //Responds with request-information
    server.resource["^/info$"]["GET"]=[](shared_ptr<HttpsServer::Response> response, shared_ptr<HttpsServer::Request> request) {
        stringstream content_stream;
        content_stream << "<h1>Request from " << request->remote_endpoint_address << " (" << request->remote_endpoint_port << ")</h1>";
        content_stream << request->method << " " << request->path << " HTTP/" << request->http_version << "<br>";
        for(auto& header: request->header) {
            content_stream << header.first << ": " << header.second << "<br>";
        }
        
        //find length of content_stream (length received using content_stream.tellp())
        content_stream.seekp(0, ios::end);
        
        *response <<  "HTTP/1.1 200 OK\r\nContent-Length: " << content_stream.tellp() << "\r\n\r\n" << content_stream.rdbuf();
    };
    
    //GET-example for the path /match/[number], responds with the matched string in path (number)
    //For instance a request GET /match/123 will receive: 123
    server.resource["^/match/([0-9]+)$"]["GET"]=[&server](shared_ptr<HttpsServer::Response> response, shared_ptr<HttpsServer::Request> request) {
        string number=request->path_match[1];
        *response << "HTTP/1.1 200 OK\r\nContent-Length: " << number.length() << "\r\n\r\n" << number;
    };
    
    //Get example simulating heavy work in a separate thread
    server.resource["^/work$"]["GET"]=[&server](shared_ptr<HttpsServer::Response> response, shared_ptr<HttpsServer::Request> /*request*/) {
        thread work_thread([response] {
            this_thread::sleep_for(chrono::seconds(5));
            string message="Work done";
            *response << "HTTP/1.1 200 OK\r\nContent-Length: " << message.length() << "\r\n\r\n" << message;
        });
        work_thread.detach();
    };
    
    //Default GET-example. If no other matches, this anonymous function will be called. 
    //Will respond with content in the web/-directory, and its subdirectories.
    //Default file: index.html
    //Can for instance be used to retrieve an HTML 5 client that uses REST-resources on this server
    server.default_resource["GET"]=[&server](shared_ptr<HttpsServer::Response> response, shared_ptr<HttpsServer::Request> request) {
        const auto web_root_path=boost::filesystem::canonical("web");
        boost::filesystem::path path=web_root_path;
        path/=request->path;
        if(boost::filesystem::exists(path)) {
            path=boost::filesystem::canonical(path);
            //Check if path is within web_root_path
            if(distance(web_root_path.begin(), web_root_path.end())<=distance(path.begin(), path.end()) &&
               equal(web_root_path.begin(), web_root_path.end(), path.begin())) {
                if(boost::filesystem::is_directory(path))
                    path/="index.html";
                if(boost::filesystem::exists(path) && boost::filesystem::is_regular_file(path)) {
                    auto ifs=make_shared<ifstream>();
                    ifs->open(path.string(), ifstream::in | ios::binary);
                    
                    if(*ifs) {
                        //read and send 128 KB at a time
                        streamsize buffer_size=131072;
                        auto buffer=make_shared<vector<char> >(buffer_size);
                        
                        ifs->seekg(0, ios::end);
                        auto length=ifs->tellg();
                        
                        ifs->seekg(0, ios::beg);
                        
                        *response << "HTTP/1.1 200 OK\r\nContent-Length: " << length << "\r\n\r\n";
                        default_resource_send(server, response, ifs, buffer);
                        return;
                    }
                }
            }
        }
        string content="Could not open path "+request->path;
        *response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << content.length() << "\r\n\r\n" << content;
    };
    
    thread server_thread([&server](){
        //Start server
        server.start();
    });
    
    // The controller provides the main loop, including an additional ios timer.
    Controller c(ios);

    //Wait for server to start so that the client can connect
    this_thread::sleep_for(chrono::seconds(1));
    
    //Client examples
    //Second Client() parameter set to false: no certificate verification
    HttpsClient client(cstr_server_url, false);
    auto r1=client.request("GET", "/match/123");
    cout << r1->content.rdbuf() << endl;

    string json_string="{\"firstName\": \"John\",\"lastName\": \"Smith\",\"age\": 25}";
    auto r2=client.request("POST", "/string", json_string);
    cout << r2->content.rdbuf() << endl;
    
    auto r3=client.request("POST", "/json", json_string);
    cout << r3->content.rdbuf() << endl;
    
    server_thread.join();
    
    return 0;
}

void default_resource_send(const HttpsServer &server, shared_ptr<HttpsServer::Response> response,
                           shared_ptr<ifstream> ifs, shared_ptr<vector<char> > buffer) {
    streamsize read_length;
    if((read_length=ifs->read(&(*buffer)[0], buffer->size()).gcount())>0) {
        response->write(&(*buffer)[0], read_length);
        if(read_length==static_cast<streamsize>(buffer->size())) {
            server.send(response, [&server, response, ifs, buffer](const boost::system::error_code &ec) {
                if(!ec)
                    default_resource_send(server, response, ifs, buffer);
                else
                    cerr << "Connection interrupted" << endl;
            });
        }
    }
}
