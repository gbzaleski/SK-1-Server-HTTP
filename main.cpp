 
// Grzegorz B. Zaleski (418494)
#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string>
#include <fstream>
#include <sys/stat.h>
#include <unordered_map>
#include <regex>
#include <streambuf>
#pragma comment(lib, "ws2_32.lib")

// Debug flag.
#ifdef DEBUG
const bool DEBUG_FLAG = true;
#else
const bool DEBUG_FLAG = false;
#endif

// Import of std structures to the project.
using std::string;
using std::unordered_map;
using std::pair;
using std::cerr;
using std::cout;
using std::regex;
using std::to_string;

// Project constants.
const string HTTPV = "HTTP/1.1";
const string CONTENT_LEN = "content-length";
const string CRLF = "\r\n";
const string D_CRLF = CRLF + CRLF;
const string CODE_SUCCES = "200";
const string CODE_FILE_MOVED = "302";
const string CODE_FILE_NOT_FOUND = "404";
const string WRONG_QUERY = "400";
const string UNKNOWN_METHOD = "501";
const string H_CONN_CLOSE = "Connection: close";
const size_t BUFFER_SIZE = 4096;

// Input validation functions.
bool correct_path(const string &path)
{
    if (regex_match(path, regex("[a-zA-Z0-9.\\-/_ ]+")))
    {
        struct stat buffer;
        return (stat(path.c_str(), &buffer) == 0);
    }
    else return false;
}
inline bool is_directory(const string &path)
{
    struct stat st_buf;
    stat (path.c_str(), &st_buf);
    return (S_ISDIR (st_buf.st_mode));
}
inline bool is_file(const string &path)
{
    struct stat st_buf{};
    stat (path.c_str(), &st_buf);
    return (S_ISREG (st_buf.st_mode));
}

// Upload database of correlated servers.
unordered_map<string, pair<string, size_t>> upload_servers(const string &path)
{
    unordered_map<string, pair<string, size_t>> servers;
    std::fstream serverlist(path, std::ios_base::in);
    string filename, server;
    size_t port;

    while (serverlist >> filename)
    {
        serverlist >> server;
        serverlist >> port;

        if (servers.find(filename) == servers.end())
            servers[filename] = make_pair(server, port);
    }

    serverlist.close();
    return servers;
}

// Function to handle and respond to messages from a client.
int handle_message(string &operation, int msg_sock,
                   unordered_map<string, pair<string, size_t>> &servers_db);

// Auxiliary function to message client.
inline void message_client(const string& message, int msg_sock);

// Checks whether target reaches out of directory.
inline bool out_target(const string &target);

// Return string trasnformed to lowercase.
string tolowerstr(string data)
{
    std::for_each(data.begin(), data.end(), [](char &c)
    {
        c = ::tolower(c);
    });
    return data;
}

// Returns pair <field-name, field-value>
inline pair <string, string> parse_header_field(const string &headerfield)
{
    pair <string, string> res;
    size_t i = 0;
    for (; i < headerfield.size() && headerfield[i] != ':'; ++i)
        res.first += headerfield[i];

    i++;
    while (headerfield[i] == ' ')
        i++;

    for (; i < headerfield.size() && headerfield[i] != ' '; ++i)
        res.second += headerfield[i];

    return res;
}

// Checks if Content-length headers contains wrong data (then returns true).
bool analyse_content_header(const string &message, size_t &i_search)
{
    i_search += CONTENT_LEN.size();
    i_search++; // Colon sign.

    while (message[i_search] == ' ')
        i_search++;

    if (message[i_search] != '0') // Only body size of zero can be accepted.
        return true;

    while (message[i_search] == '0')
        i_search++;

    while (message[i_search] == ' ')
        i_search++;

    return message[i_search] != CRLF[0] || message[i_search + 1] != CRLF[1];
}

// Auxiliary global paths and flags.
string directory_name, correlated_servers;
bool close_flag, send_error_flag;

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        cerr << "Usage: " << argv[0] << " directory_name correlated_servers_file_name [optional port]\n";
        exit(EXIT_FAILURE);
    }

    // Check if directory exists.
    directory_name = string(argv[1]);
    if (!correct_path(directory_name)
        || is_file(directory_name))
    {
        cerr << "Directory path error\n";
        exit(EXIT_FAILURE);
    }

    // Check if corelated files file exists.
    correlated_servers = string(argv[2]);
    if (!correct_path(correlated_servers)
        || is_directory(correlated_servers))
    {
        cerr << "Corelated servers path error\n";
        exit(EXIT_FAILURE);
    }

    auto servers_db = upload_servers(correlated_servers);

    // Establish port.
    size_t port = 8080; // Default port.
    if (argc != 3)
        port = atoi(argv[3]);


    // Create a socket.
    int sock = socket(AF_INET, SOCK_STREAM, 0); // Creating IPv4 TCP socket.
    if (sock < 0)
    {
        cerr << "Socket error\n";
        exit(EXIT_FAILURE);
    }

    // Bind the socket.
    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY); // Listening on all interfaces.
    server_address.sin_port = htons(port); // Listening on given port.
    inet_pton(AF_INET,"0.0.0.0", &server_address.sin_addr);

    if (bind(sock, (sockaddr *) &server_address, sizeof(server_address)) < 0)
    {
        cerr << "Bind error\n";
        exit(EXIT_FAILURE);
    }

    // Prepare socket.
    if (listen(sock, SOMAXCONN) < 0)
    {
        cerr << "Listen error\n";
        exit(EXIT_FAILURE);
    }

    if (DEBUG_FLAG)
        cout << "Server is accepting connections on port "<<  ntohs(server_address.sin_port) << "\n";

    // Server is online.
    while (true)
    {
        struct sockaddr_in client_address;
        socklen_t client_address_len;
        size_t len;
        int msg_sock;
        char buffer[BUFFER_SIZE];
        close_flag = false;
        send_error_flag = false;
        string message;
        size_t i_search = 0;

        client_address_len = sizeof(client_address);
        // Get client connection from the socket.
        msg_sock = accept(sock, (struct sockaddr *) &client_address, &client_address_len);
        if (msg_sock < 0)
        {
            cerr << "Accepting error\n";
            exit(EXIT_FAILURE);
        }

        do
        {
            len = read(msg_sock, buffer, sizeof(buffer));
            if (len < 0)
            {
                cerr << "Reading from client error.\n";
                exit(EXIT_FAILURE);
            }
       
            message += string(buffer, len);
            i_search = 0;

            size_t i = message.find(D_CRLF);
            int handlecode = 0;

            // Request cannot have body part.
            while ((i_search = tolowerstr(message).find(CONTENT_LEN, i_search)) != string::npos)
            {
                if (analyse_content_header(message, i_search))
                {
                    if (DEBUG_FLAG)
                        cout << "Content-Length error.\n";
                    message_client(HTTPV + " " + WRONG_QUERY + " " + "Wrong query." + CRLF
                          + H_CONN_CLOSE + CRLF // Header to let client know that connection is to be shut down.
                          + CRLF, msg_sock);
                    handlecode = -1;
                }
            }

            while (i != string::npos && handlecode != -1)
            {
                auto inp = message.substr(0, i + CRLF.size());
                handlecode = handle_message(inp, msg_sock, servers_db);
                message = message.substr(i + D_CRLF.size());
                i = message.find(D_CRLF);
                i_search = 0;
                // Case with ending connection after one request.
                if (close_flag)
                {
                    // Breaks both loops.
                    len = 0;
                    break;
                }

            }

            if (handlecode == -1 || send_error_flag)
                break;

        } while (len > 0);

        if (DEBUG_FLAG)
            cout <<"Ending connection.\n";

        if (close(msg_sock) < 0)
        {
            cerr << "Closing error\n";
            exit(EXIT_FAILURE);
        }
    }

    return 0;
}

// Check if headers are properly constructed.
// Ignores headers which are not considered by server.
// Also sets double connection and content flags.
bool wrong_headers(string &header_field)
{
    size_t header_end = header_field.find(CRLF);
    bool connection_flag = false;
    bool content_flag = false;
    while (header_end != string::npos)
    {
        string header = header_field.substr(0, header_end);
        header_field = header_field.substr(header_end + CRLF.size());

        auto header_values = parse_header_field(tolowerstr(header));

        if (header_values.first.empty() || header_values.second.empty()) // Wrong headers format.
            return true;

        if (header_values.first == "connection")
        {
            if (header_values.second == "close")
                close_flag = true;

            if (connection_flag)
                return true;

            connection_flag = true;
        }

        if (header_values.first == "content-length")
        {
            if (content_flag)
                return true;

            content_flag  = true;
        }

        header_end = header_field.find(CRLF);
    }
    return false;
}

// Function to handle and respond to messages from a client.
int handle_message(string &operation, int msg_sock,
                   unordered_map<string, pair<string, size_t>> &servers_db)
{
    // Parsing message to status-line (here called operation)
    // and headers.
    size_t sline_end = operation.find(CRLF);
    string received_headers = operation.substr(sline_end + CRLF.size());
    operation = operation.substr(0, sline_end);

    string method = operation.substr(0, operation.find(' '));
    operation = operation.substr(method.size() + 1);

    string target = operation.substr(0, operation.find(' '));
    operation = operation.substr(target.size() + 1);

    // Unimplemented method case.
    if (method != "GET" && method != "HEAD")
    {
        if (DEBUG_FLAG)
            cout << "This method is not accepted.\n";
        message_client(HTTPV + " " + UNKNOWN_METHOD + " " + "This method is not accepted." + CRLF
                       + CRLF, msg_sock);
        return 0;
    }

    // Incorrect request.
    if (operation != HTTPV || target.empty() || target[0] != '/' || wrong_headers(received_headers))
    {
        if (DEBUG_FLAG)
            cout << "Wrong query.\n";
        message_client(HTTPV + " " + WRONG_QUERY + " " + "Wrong query." + CRLF
              + H_CONN_CLOSE + CRLF // Header to let client know that connection is to be shut down.
              + CRLF, msg_sock);
        return -1;
    }

    // Handling correct and parsed request.
    if (method == "GET" || method == "HEAD")
    {
        string start_line;
        string headers;
        string file_body;
        // File found in the directory.
        if (!out_target(target) && correct_path(directory_name + target)
            && is_file(directory_name + target) && regex_match(target, regex("[a-zA-Z0-9./-]+")))
        {
            start_line = HTTPV + " " + CODE_SUCCES + " File has been located." + CRLF;

            if (DEBUG_FLAG)
                cout << "File found!\n";

            // Upload data from file.
            std::ifstream t(directory_name + target);
            file_body = string((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
            headers += "Content-Length: " + to_string(file_body.size()) + CRLF;

            // HEAD method send empty body.
            if (method != "GET")
            {
                file_body = "";
            }

        }
        // Failed to found file in the directory.
        else
        {
            // Found in correlated servers list.
            if (servers_db.find(target) != servers_db.end())
            {
                if (DEBUG_FLAG)
                    cout << "File found on correlated server!\n";

                start_line = HTTPV + " " + CODE_FILE_MOVED + " File has been located on correlated server." + CRLF;
                headers += string("Location: http://") + servers_db[target].first + string(":")
                        + to_string(servers_db[target].second) + target + CRLF;
            }
            else
            {
                if (DEBUG_FLAG)
                    cout << "File not found anywhere!\n";
                start_line = HTTPV + " " + CODE_FILE_NOT_FOUND + " Failed to locate file." + CRLF;
            }
        }

        message_client(start_line + headers + CRLF + file_body, msg_sock);
        return 1;
    }
    else
    {
        message_client(HTTPV + " " + WRONG_QUERY + " " + "Wrong query." + CRLF
            + H_CONN_CLOSE + CRLF // Header to let client know that connection is to be shut down.
            + CRLF, msg_sock);
        return -1;
    }
}

// Checks whether target reaches out of directory.
inline bool out_target(const string &target)
{
    int depth = 0;
    for (size_t i = 0; i < target.size(); ++i)
    {
        if (target[i] == '/')
        {
            i++;
            if (i < target.size() && target[i] == '.')
            {
                i++;
                if (i < target.size() && target[i] == '.')
                {
                    depth--;
                    if (depth < 0)
                    {
                        if (DEBUG_FLAG)
                            cout << "File reaches out of a dir!\n";

                        return true;
                    }
                }
            }
            else depth++;
        }
    }
    return false;
}

// Auxiliary function to message client.
// Sets flag to true if error occured.
inline void message_client(const string &message, int msg_sock)
{
    size_t snd_len = send(msg_sock, message.c_str(), message.size(), MSG_NOSIGNAL);

    send_error_flag = (snd_len != message.size());
}