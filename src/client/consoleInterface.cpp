#include "headers/consoleInterface.hpp"
#include "headers/fileManager.hpp"
#include "../hashing/headers/hash.h"
#include <sstream>
using std::cout;
using std::endl;
using std::string;
using std::vector;


Client::ConsoleInterface::ConsoleInterface(): state(State::up), messageState(MessageState::none) {}

Client::ConsoleInterface::~ConsoleInterface() {}

void Client::ConsoleInterface::handleInputUp(Client& client, std::vector<std::string> input){


    if(input[0] == "help")
    {
        cout << "Lista komend:" << endl
        << "help - wypisz liste dostepnych komend" << endl
        << "connect - polacz z serwerem" << endl
        << "ls - pokaz zawartosc katalogu z plikami, ktore udostepniasz (wymagane polaczenie z serwerem)" << endl
        << "disconnect - rozlacz sie z serwerem" << endl
        << "quit - wylacz program" << endl;
    }   else if(input[0] == "connect")
    {
        client.connectTo(client.getServer());

        //wait for 210 before anything else

        state = State::connected;
    }   else if (input[0] == "quit")
    {
        state = State::down;
    }   else
    {
        cout << "Nieprawidlowa komenda! Wpisz 'help', aby zobaczyc liste komend." << endl;
    }
}
/*
    1. Wyświetl zawartość katalogu
    2. Pobierz plik
    3. Uaktualnij stan plików
    4. Zakończ pobierać plik
    5. Zakończ udostępniać plik

*/
void Client::ConsoleInterface::handleInputConnected(Client& client, std::vector<std::string> input){

    if(input[0] == "help")
    {
        cout << "Lista komend:" << endl
        << "help - wypisz liste dostepnych komend" << endl
        << "connect - polacz z serwerem" << endl
        << "ls - pokaz zawartosc katalogu z plikami, ktore udostepniasz (wymagane polaczenie z serwerem)" << endl
        << "disconnect - rozlacz sie z serwerem" << endl
        << "quit - wylacz program" << endl;
    }   else if(input[0] == "connect")
    {
        cout << "Jestes juz polaczony!" << endl;
    }   else if(input[0] == "ls")
    {
        client.fileManager->printFolderContent();
    }   else if (input[0] == "quit")
    {
        state = State::down;
    }   else if (input[0] == "file_download")
    {
        if(input.size() < 2)
        {
            cout << "Nieprawidlowa komenda! Wpisz 'help', aby zobaczyc liste komend." << endl;
        }
        else
        {
            client.sendAskForFile(input[1]);                    // wysłanie żądania o plik do serwera
            messageState = MessageState::wait_for_file_info;    // ustawienie stanu na czekanie na odpowiedź od serwera o pliku
            chosenFile = input[1];                              // zapisanie wybranego pliku
        }

    }   else
    {
        cout << "Nieprawidlowa komenda! Wpisz 'help', aby zobaczyc liste komend." << endl;
    }
/*
    switch(input){
        case 1:
                printFolderContent();
                break;
        
        case 2:
                //TODO
                break;
        case 3:
                break;

        case 4:
                if(state == State::seeding) {stopSeeding();}
                if(state == State::leeching || state == State::both) {stopLeeching();}
                break;

        case 5:
                if(state == State::both) {stopSeeding();}
                break;
                
        case 9:
                state = State::down;
                break;

        default:
                //TODO
                break;

    }
*/
}

void Client::ConsoleInterface::processCommands(const char* buf)
{
    buffer.insert(buffer.end(), buf, buf+strlen(buf));
    for(auto it = buffer.begin(); it!=buffer.end();)
    {
        if(*it==10)
        {
            commandQueue.emplace(std::string(buffer.begin(), it));
            it=buffer.erase(buffer.begin(), it+1);
        }
        else
        {
            ++it;
        } 
    }
}

std::vector<std::string> Client::ConsoleInterface::splitBySpace(std::string input)
{
    std::string buf;
    std::stringstream ss(input);

    std::vector<std::string> tokens;

    while (ss >> buf)
        tokens.push_back(buf);

    return tokens;
}

void Client::ConsoleInterface::handleInput(Client& client)
{
    if(!commandQueue.empty())
    {
        std::string input=commandQueue.front();
        commandQueue.pop();
        vector<std::string> tokens = splitBySpace(input);
        if(tokens.size() == 0) tokens.push_back("");
        if(state == State::up)  handleInputUp(client, tokens);
        else if(state == State::connected || state == State::seeding || state == State::seeding || state == State::both) handleInputConnected(client, tokens); // prawie takie same
    }
}

void Client::ConsoleInterface::stopSeeding()
{
    //TODO
}

void Client::ConsoleInterface::stopLeeching()
{
    //TODO
}

State Client::ConsoleInterface::getState(){
    return state;
}

MessageState Client::ConsoleInterface::getMessageState(){
    return messageState;
}

void Client::ConsoleInterface::setMessageState(MessageState newState){
    messageState = newState;
}

std::string Client::ConsoleInterface::getChosenFile(){
    return chosenFile;
}

