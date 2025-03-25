## 3. Connection Acceptance Sequence Diagram

This sequence diagram shows the interaction between the main thread, a worker thread, and a client socket during connection acceptance and processing.

---

```mermaid
sequenceDiagram
    participant Main as Main Thread (Accept Loop)
    participant Worker as Worker Thread
    participant Client as Client Socket

    Main->>Client: accept() new connection
    Main-->>Worker: Pass client FD to worker's epoll instance
    Worker->>Client: Wait for EPOLLIN event via epoll_wait
    Worker->>Client: Call recv() to read request
    Worker->>Client: Call send() to write HTTP response
```

---

```mermaid
%% Server Architecture Diagram
graph TD
    Main[Main Thread] -->|Creates| ServerSocket[Server Socket]
    Main -->|Creates| MainEpoll[Main Epoll Instance]
    Main -->|Spawns| WorkerThreads[Worker Threads]

    subgraph Thread Pool
        WorkerThread1[Worker Thread 1] -->|Has| Epoll1[Epoll Instance 1]
        WorkerThread2[Worker Thread 2] -->|Has| Epoll2[Epoll Instance 2]
        WorkerThreadN[Worker Thread N] -->|Has| EpollN[Epoll Instance N]
    end

    ClientConnections[Client Connections] -->|Accepted by| ServerSocket
    ServerSocket -->|Distributes via| LoadBalancer[Round-Robin Load Balancer]
    LoadBalancer -->|Assigns to| Epoll1
    LoadBalancer -->|Assigns to| Epoll2
    LoadBalancer -->|Assigns to| EpollN
```

---

```mermaid
%% Sequence Diagram
sequenceDiagram
    participant Client
    participant MainThread
    participant WorkerThread
    participant Epoll

    Client->>MainThread: TCP SYN
    MainThread->>MainThread: accept()
    MainThread->>WorkerThread: Assign via Round-Robin
    WorkerThread->>Epoll: epoll_ctl(ADD)
    Epoll-->>WorkerThread: EPOLLIN Event
    WorkerThread->>Client: recv()
    Client->>WorkerThread: HTTP Request
    WorkerThread->>WorkerThread: Process Request
    WorkerThread->>Client: send()
    alt Keep-Alive
        WorkerThread->>Epoll: Maintain Connection
    else Close
        WorkerThread->>Epoll: epoll_ctl(DEL)
        WorkerThread->>Client: FIN
    end
```

---

```mermaid
%% Connection Lifecycle State Diagram
stateDiagram-v2
    [*] --> Closed
    Closed --> Listening: socket()/bind()/listen()
    Listening --> Accepting: accept()
    Accepting --> Registered: epoll_ctl(ADD)
    Registered --> Processing: EPOLLIN
    Processing --> Responding: send()
    Responding --> KeepAlive: Connection: keep-alive
    Responding --> Closing: Connection: close
    KeepAlive --> Processing: EPOLLIN
    Closing --> Closed: close()
    Error --> Closed: On EPOLLERR/EPOLLHUP
```

---
