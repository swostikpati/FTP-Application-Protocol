# FTP Application Protocol
This project is an implementation of a simplified version of the FTP (File Transfer Protocol) application protocol. It consists of two separate programs: an FTP server and an FTP client. The FTP server is responsible for maintaining FTP sessions and providing file access, while the FTP client is split into two components - an FTP user interface and an FTP client to make requests to the FTP server.

## Project Overview
The primary goal of this project is to create a basic FTP system, allowing users to interact with a server to perform file transfers and manage remote files. The system is designed to support concurrent connections, enabling multiple clients to interact with the server simultaneously. Key features and components of the project include:

### FTP Server
The server is responsible for handling incoming FTP client connections, maintaining FTP sessions, and providing file access to clients. It supports concurrent connections, allowing multiple clients to interact with it simultaneously.

### FTP Client
The FTP client is divided into two components:

**FTP User Interface:** This component provides a simple command-line interface for users to interact with the FTP server. Users can input commands to perform various operations, such as uploading, downloading, listing files, and navigating directories on the server.

**FTP Client Core:** The core client component communicates with the FTP server, sending requests and receiving responses. It manages the control connection with the server and handles data transfer using select() and fork() as appropriate.