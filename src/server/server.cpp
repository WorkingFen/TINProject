#include "headers/server.hpp"

/***
 * std::string text;
 * for(auto i : msg.buffer) if(i < 'A' || i > 'z') text += std::to_string(i); else text += i;
 * std::cout << text << std::endl;
 ***/   


// Wait for SIGINT to occur
void server::Server::signal_waiter() {
    int sig;

    sigwait(&signal_set, &sig);
    if(sig == SIGINT) {
        std::cout << "\rReceived SIGINT. Exiting..." << std::endl;
        sigint_flag = true;
    }
}

// Set signal_set to SIGINT
void server::Server::set_SIGmask() {
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGINT);
    int status = pthread_sigmask(SIG_BLOCK, &signal_set, NULL);
    if(status != 0)
        throw std::runtime_error("Setting signal mask failed");
}

// Close all connections; Before that, send msg about closing
void server::Server::shutdown() {
    while(clients.size() != 0) {
        cts_it = clients.begin();
        msg::Message farewell(211);
        farewell.sendMessage(cts_it->socket);
        close_ct();
    }
    close(listener);
}

// Add new file and get it's pointer or get existing pointer
server::file* server::Server::add_file(int size, std::string name) {
    auto e_file = files.find(name);
    if(e_file != files.end()) return &(e_file->second);

    file n_file(name, size);
    auto i_file = files.insert({n_file.name, n_file});
    return &(i_file.first->second);
}

// Add new block and get it's pointer or get existing pointer
server::block* server::Server::add_block(server::file& c_file, std::string hash, uint no) {
    if(no < c_file.blocks.size()) { 
        if(c_file.blocks[no].hash == hash) return &c_file.blocks[no];
        else return nullptr;
    }
    else {
        block n_block(hash);
        c_file.blocks.push_back(n_block);
        return &(c_file.blocks.back());
    }
}

// Add new owner if block information is correct 
int server::Server::add_block_owner(server::file& c_file, std::string hash, uint no) {
    if(no >= c_file.blocks.size()) return MSG_RNGERROR;

    if(c_file.blocks[no].empty) c_file.blocks[no] = block(hash);

    if(c_file.blocks[no].hash == hash) {
        add_owner(c_file.blocks[no], &*cts_it);
        return MSG_OK;
    }
    else return MSG_ERROR;
}

// Add new owner to block information if not already there
void server::Server::add_owner(server::block& c_block, server::client* owner) {
    for(auto e_owner : c_block.owners) if(e_owner->socket == owner->socket) return;

    c_block.owners.push_back(owner);
}

// Get existing file by its name, else nullptr
server::file* server::Server::get_file(std::string name) {
    auto e_file = files.find(name);
    if(e_file != files.end()) return &(e_file->second);
    else return nullptr;
}

// Get existing block by its number, else nullptr
server::block* server::Server::get_block(server::file& c_file, uint no) {
    if(no < c_file.blocks.size()) return &(c_file.blocks[no]);
    else return nullptr;
}

// Delete file from vector of files
void server::Server::delete_file(server::file& c_file) {
    auto it = files.find(c_file.name);
    files.erase(it);
}

// Delete block by overwriting it
bool server::Server::delete_block(server::file& c_file, int no) {
    c_file.blocks[no] = block();

    for(auto c_block : c_file.blocks) if(!c_block.empty) return false;
    return true;
}

bool server::Server::delete_owner(server::block& c_block) {
    for(auto i = c_block.owners.begin(); i != c_block.owners.end(); i++)
        if(&(*i)->socket == &cts_it->socket) {
            c_block.owners.erase(i);
            break;
        }
    
    if(c_block.owners.empty()) return true;
    else return false;
}

// Delete owner if it exists and if block isn't empty, then check if block's owners vector isn't empty
bool server::Server::delete_owner(server::block& c_block, uint ip, int port) {
    if(c_block.empty) return false;

    for(auto c_owner = c_block.owners.begin(); c_owner != c_block.owners.end(); c_owner++)
        if(*(&(*c_owner)->address.sin_port) == port && *(&(*c_owner)->address.sin_addr.s_addr) == ip) {
            c_block.owners.erase(c_owner);
            break;
        }
    
    if(c_block.owners.empty()) return true;
    else return false;
}

// Get least occupied seeder and its block which client doesn't have
std::pair<server::client*, int> server::Server::find_least_occupied(server::file& c_file, std::vector<int> no_blocks) {
    client* best_ct;
    int best_no;
    bool got;

    for(uint i = 0; i <= c_file.blocks.size(); i++) {
        if(i == c_file.blocks.size()) return std::pair<client*, int>({nullptr,i});
        if(!c_file.blocks[i].empty) {
            got = false;
            if(!no_blocks.empty()) {
                for(uint tmp : no_blocks)
                    if(tmp == i) {
                        got = true;
                        break;
                    }
            }
            if(got) continue;
            best_ct = c_file.blocks[i].owners.front();
            best_no = i;
            if(best_ct->no_leeches == 0) return std::pair<client*, int>({best_ct, best_no});
            break;
        }
    }
    
    for(uint i = best_no + 1; i < c_file.blocks.size(); i++) {
        if(c_file.blocks[i].empty) continue;

        got = false;
        if(!no_blocks.empty()) {
            for(uint tmp : no_blocks)
                if(tmp == i) {
                    got = true;
                    break;
                }
        }
        if(got) continue;
        
        for(auto j : c_file.blocks[i].owners) {
            if(j->no_leeches == 0) return std::pair<client*, int>({j,i});
            else if(best_ct->no_leeches > j->no_leeches) {
                best_ct = j;
                best_no = i;
            }
        }
    }

    return std::pair<client*, int>({best_ct,best_no});
}

server::Server::Server(const char srv_ip[15], const int& srv_port) : sigint_flag(false) {
    set_SIGmask();
    signal_thread = std::thread(&Server::signal_waiter, this);

    timeout.tv_sec = 120;
    timeout.tv_usec = 0;

    socket_srv();
    bind_srv(srv_ip, srv_port);
    listen_srv();   
}

// Main program loop
void server::Server::run_srv() {
    int enable = 1;
    if(setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
        std::cerr << "setting SO_REUSEADDR option on socket " << listener << " failed" << std::endl;

    while(!sigint_flag){
        select_cts();
        accept_srv();
        check_cts();
    }
    close_srv();
}

// Cancel server's thread and close its socket
void server::Server::close_srv() {
    pthread_cancel(signal_thread.native_handle());
    signal_thread.join();
    shutdown();
}

// Create listener socket
void server::Server::socket_srv() {
    if((listener = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        throw std::runtime_error("Can't create a socket!");

    max_fd = listener + 1;
}

// Bind socket to ip:port 
void server::Server::bind_srv(const char srv_ip[15], const int& srv_port) {
    server.sin_family = AF_INET;
    server.sin_port = htons(srv_port);
    if(inet_pton(AF_INET, srv_ip, &server.sin_addr) == -1)
        throw std::domain_error("Improper IPv4 address");

    if(bind(listener, (sockaddr*)&server, sizeof(server)) == -1)
        throw std::runtime_error("Can't bind to IP:port");

#ifdef SOCKNAME
    socklen_t sa_len = sizeof(server);
    if(getsockname(listener, (sockaddr*)&server, &sa_len) == -1)
        throw std::runtime_error("Can't get socket name");    
    std::cout << "Successfully connected and listening at: " << srv_ip << ":" << ntohs(server.sin_port) << std::endl;
#endif
}

// Start listening on listener socket, with SOMAXCONN connections
void server::Server::listen_srv() {
    if(listen(listener, SOMAXCONN) == -1)
        throw std::runtime_error("Can't listen!");
}

// Select clients
void server::Server::select_cts() {
    FD_ZERO(&bits_fd);
    FD_SET(listener, &bits_fd);

    for(auto i : clients)
        FD_SET(i.socket, &bits_fd);

    if(select(max_fd, &bits_fd, (fd_set*)0, (fd_set*)0, &timeout) == -1)
        throw std::runtime_error("Select call failed");
}

// Accept new client
void server::Server::accept_srv() {
    if(FD_ISSET(listener, &bits_fd)){
        sockaddr_in ct_addr;
        socklen_t client_size = sizeof(ct_addr);
        int ct_socket = accept(listener, (sockaddr*)&ct_addr, &client_size);
        client ct(ct_addr, ct_socket);
        clients.push_back(ct);
        max_fd = std::max(max_fd, ct_socket+1);

        if(clients.back().socket == -1)
            throw std::runtime_error("Problem with client connecting");
        else {
            msg::Message reply(210);
            reply.writeInt(pieceSize);
            reply.sendMessage(ct_socket);
#ifdef CTNAME
            memset(host, 0, NI_MAXHOST);
            memset(svc, 0, NI_MAXSERV);

            if(getnameinfo((sockaddr*)&ct_addr, sizeof(ct_addr), host, NI_MAXHOST, svc, NI_MAXSERV, 0)) {
                std::cout << host << " connected on " << svc << std::endl;
            }
            else {
                inet_ntop(AF_INET, &ct_addr.sin_addr, host, NI_MAXHOST);
                std::cout << host << " connected on " << ntohs(ct_addr.sin_port) << std::endl;
            }
#endif
        }
    }
}

// Check clients' messages
void server::Server::check_cts() {
    for(cts_it = clients.begin(); cts_it != clients.end(); ) {
        if(FD_ISSET(cts_it->socket, &bits_fd)) {
            int bytes_rcv = read_srv();
            if(bytes_rcv == SRV_ERROR || bytes_rcv == SRV_NOCONN) {
                close_ct();
                continue;
            }
        }
        ++cts_it;
    }
}

// Read message from client with ID: client_id
int server::Server::read_srv() {
    if(!msg_manager.assembleMsg(cts_it->socket)) {
        if(msg_manager.lastReadResult() <= 0) return SRV_ERROR;
        else return SRV_OK;
    }

    msg::Message msg = msg_manager.readMsg(cts_it->socket);

    if(msg.type == -1) {
#ifdef LOGS
        std::cerr << "There was a connection issue" << std::endl;
#endif        
        return SRV_ERROR;
    }
    // Share file
    else if(msg.type == 101) {
        file* c_file = add_file(msg.readInt(), msg.readString(msg.readInt()));  // Weird specification of C++ compiler?
        for(int no_block = 0; msg.buf_length > 0; no_block++) {
            block* c_block = add_block(*c_file, msg.readString(64), no_block);
            if(c_block == nullptr) {}             // Error
            else add_owner(*c_block, &*cts_it);
        }
#ifdef LOGS
        std::cout << std::endl << "Message type: " << msg.type << std::endl;
        std::cout << "File name: " << c_file->name << std::endl;
        std::cout << "File size: " << c_file->size << std::endl;
        for(auto i : c_file->blocks)
            std::cout << "Piece hash: " << i.hash << std::endl;
#endif
#ifdef FILES
        for(auto i : files) {
            std::cout << std::endl << "File: " << i.second.name << std::endl;
            for(uint j = 0; j < i.second.blocks.size(); j++) {
                std::cout << j << ". Block: " << i.second.blocks[j].hash << std::endl;
                for(auto k : i.second.blocks[j].owners) {
                    std::cout << "--- " << k->address.sin_addr.s_addr << ":" << k->address.sin_port << std::endl;
                }
            }
        }
#endif
    }
    // List all files
    else if(msg.type == 102) {
#ifdef LOGS
        std::cout << std::endl << "Message type: " << msg.type << std::endl;
#endif 
        if(!files.empty())
            for(auto c_file : files) {
                msg::Message file_list(201);
                file_list.writeInt(c_file.first.size());
                file_list.writeString(c_file.first);
                file_list.writeInt(c_file.second.size);
                file_list.sendMessage(cts_it->socket);
            }
        else {}            // Error
    }
    /*else if(msg.type == 103) {
        auto foo = files.find(msg.readString(msg.readInt()));
        int no_block = msg.readInt();
        if(delete_owner(*get_block(foo->second, no_block)) && delete_block(foo->second, no_block)){
            std::string name = foo->second.name;
            delete_file(foo);
#ifdef LOGS
            std::cout << std::endl << "File " << name << " has been deleted" << std::endl;
#endif
        }
#ifdef LOGS
        std::cout << std::endl << "Message type: " << msg.type << std::endl;
#endif
    }*/
    // File download request
    else if(msg.type == 104) {
#ifdef LOGS
        std::cout << std::endl << "Message type: " << msg.type << std::endl;
#endif
        file* c_file = get_file(msg.readString(msg.readInt()));
        if(c_file == nullptr) {}            // Error
        else {
            msg::Message file_cfg(201);
            file_cfg.writeInt(c_file->name.size());
            file_cfg.writeString(c_file->name);
            file_cfg.writeInt(c_file->size);
            file_cfg.sendMessage(cts_it->socket);
        }
    }
    // Inform about block possession
    else if(msg.type == 105) {
        file* c_file = get_file(msg.readString(msg.readInt()));
        if(c_file == nullptr) {}            // Error
        else {
            int no_block = msg.readInt();
            switch(add_block_owner(*c_file, msg.readString(64), no_block)) {
                case MSG_RNGERROR: {
                            // Error
                    break;
                }
                case MSG_ERROR: {
                            // Error
                    break;
                }
            }
#ifdef LOGS
            std::cout << std::endl << "Message type: " << msg.type << std::endl;
            std::cout << "File name: " << c_file->name << std::endl;
            std::cout << "Piece number: " << no_block << std::endl;
            std::cout << "Piece hash: " << c_file->blocks[no_block].hash << std::endl;
#endif
        }
    }
    // Give client information about block to download
    else if(msg.type == 106) {
#ifdef LOGS
        std::cout << std::endl << "Message type: " << msg.type << std::endl;
#endif
        file* c_file = get_file(msg.readString(msg.readInt()));
        if(c_file == nullptr) {}                // Error
        else {
            std::vector<int> blocks_no;
            while(msg.buf_length > 0)
                blocks_no.push_back(msg.readInt());
            auto lo_ct = find_least_occupied(*c_file, blocks_no);
            if(lo_ct.first == nullptr) {}         // Error
            else {
                lo_ct.first->no_leeches++;
                msg::Message file_info(202);
                file_info.writeInt(c_file->name.size());
                file_info.writeString(c_file->name);
                file_info.writeInt(lo_ct.second);
                file_info.writeString(c_file->blocks[lo_ct.second].hash);
                file_info.writeInt(lo_ct.first->address.sin_addr.s_addr);
                file_info.writeInt(lo_ct.first->address.sin_port);
                file_info.sendMessage(cts_it->socket);
            }
        }
    }
    // Wrong hash from seeder
    else if(msg.type == 107) {
        file* e_file = get_file(msg.readString(msg.readInt()));
        if(e_file == nullptr) {}           // Error
        else {
            int no_block = msg.readInt();
            int o_addr = msg.readInt();
            int o_port = msg.readInt();
#ifdef LOGS
            std::cout << std::endl << o_addr << ":" << o_port << std::endl;
#endif
            block* e_block = get_block(*e_file, no_block);
            if(e_block == nullptr) {}       // Error
            else {
                if(delete_owner(*e_block, o_addr, o_port) && delete_block(*e_file, no_block)){
#ifdef LOGS
                    std::string name = e_file->name;
#endif
                    delete_file(*e_file);   
#ifdef LOGS
                    std::cout << std::endl << "File " << name << " has been deleted" << std::endl;
#endif
                }      
            }      
        }
#ifdef LOGS
        std::cout << std::endl << "Message type: " << msg.type << std::endl;
#endif
    }
    /*else if(msg.type == 108) {
        auto foo = files.find(msg.readString(msg.readInt()));
        int no_block = msg.readInt();
        if(delete_owner(*get_block(foo->second, no_block)) && delete_block(foo->second, no_block)){
            std::string name = foo->second.name;
            delete_file(foo);
#ifdef LOGS
            std::cout << std::endl << "File " << name << " has been deleted" << std::endl;
#endif
        }
#ifdef LOGS
        std::cout << std::endl << "Message type: " << msg.type << std::endl;
#endif
    }*/
    // Client keepalive
    else if(msg.type == 110) {
#ifdef KEEPALIVE
        std::cout << "KeepAlive on socket number: " << cts_it->socket << std::endl;
#endif
        cts_it->timeout = std::chrono::high_resolution_clock::now();
        msg::Message reply(209);
        reply.sendMessage(cts_it->socket);
    }
    // Dissconnect current client
    else if(msg.type == 111) {
#ifdef LOGS
        std::cout << "The client disconnected" << std::endl;
#endif
        return SRV_NOCONN;
    }
    else {
#ifdef LOGS
        std::cout << "Received: " << msg.type << std::endl;
        std::cout << "Message type is unknown" << std::endl;
        std::cout << "Message: ";
        for(char c : msg.buffer) std::cout << c;
        std::cout << std::endl;
        std::cout << "Message bytes: " << std::endl;
        for(char c : msg.buffer) std::cout << static_cast<int>(c) << " ";
        std::cout << std::endl;
#endif
    }

    return SRV_OK;
}

// Close connection with client whose ID: client_id
void server::Server::close_ct() {
#ifdef LOGS
    memset(host, 0, NI_MAXHOST);
    memset(svc, 0, NI_MAXSERV);

    if(getnameinfo((sockaddr*)&(cts_it->address), sizeof(cts_it->address), host, NI_MAXHOST, svc, NI_MAXSERV, 0)) {
        std::cout << "Closing socket of: " << host << ":" << svc << std::endl;
    }
    else {
        inet_ntop(AF_INET, &(cts_it->address).sin_addr, host, NI_MAXHOST);
        std::cout << "Closing socket of: " << host << ":" << ntohs(cts_it->address.sin_port) << std::endl;
    }
#endif
    if(close(cts_it->socket) == -1)
        throw std::runtime_error("Couldn't close client's socket");
    else cts_it = clients.erase(cts_it);
}

server::Server::~Server() {
#ifdef LOGS
    std::cout << "Disconnected" << std::endl;
#endif
}