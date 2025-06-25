# Custom-Transport-Protocol-Sockets
#### Packet-oriented transport protocol over UDP with sequenced delivery and sliding window flow control.  
Note:
In this program the system call pidfd_getfd() has been used to get the file descriptor from another process.
Ensure that accessing other processes is not restricted on your system (proc/sys/kernel/yama/ptrace_scope is set to 0).

Instructions to execute:  
make -f makelib.mk  
make -f makeinit.mk  
make -f makeuser.mk  
./initksocket  
./user2 (\<Source Port> \<Destination Port>)   [arguments optional, default ports: 6000 5000]  
./user1 (\<Source Port> \<Destination Port>)   [arguments optional, default ports: 5000 6000]  

Table of Average Number of Transmissions per Message vs Drop Probability:  
Number of messages generated from file = 97 (512 byte)  
<table>
  <tr>
    <th>Drop prob(p)</th>
    <th>#Transmissions</th>
    <th>Average</th>
  </tr>
  <tr>
    <td>0.05</td>
    <td>120</td>
    <td>1.24</td>                     
  </tr>
  <tr>
    <td>0.10</td>
    <td>139</td>
    <td>1.43</td>                         
  </tr>
  <tr>
    <td>0.15</td>
    <td>147</td>
    <td>1.52</td>                         
  </tr>
  <tr>
    <td>0.20</td>
    <td>167</td>
    <td>1.72</td>                         
  </tr>
  <tr>
    <td>0.25</td>
    <td>192</td>
    <td>1.98</td>                         
  </tr>
  <tr>
    <td>0.30</td>
    <td>230</td>
    <td>2.37</td>                         
  </tr>
  <tr>
    <td>0.35</td>
    <td>281</td>
    <td>2.90</td>                         
  </tr>
  <tr>
    <td>0.40</td>
    <td>343</td>
    <td>3.53</td>                         
  </tr>
  <tr>
    <td>0.45</td>
    <td>357</td>
    <td>3.68</td>                         
  </tr>
  <tr>
    <td>0.50</td>
    <td>369</td>
    <td>3.80</td>           
  </tr>
</table>

<code>
Data Structures used:
1. buff - Used to model the internal send and receive buffers of the ksocket
    Field     Type      Description
    start     int       start index (first unacknowledged message in sender, first expected message in receiver)
    end       int       end index (end of sent messages in sender, end of window in receiver)
    msg_cnt   int       number of active messages in buffer (unsent/unacknowledged in sender, unread in receiver)
    arr       char[][]  array storing messages
</code>
<code>
2. window - Used to model the sender and receiver windows for the ksocket sliding window flow control
    Field       Type    Description
    wnd_size    int     size of the window
    start_seq   int     starting sequence number (first unacknowledged seq no in sender, first expected seq no in receiver)
    unsent      int     count of unsent messages in sender buffer
    nospace     int     flag indicating whether there is (or was) no space in receiver buffer
</code>
<code>
3. ksock - Used to model and implement the ksocket
    Field        Type                 Description
    mutex        pthread_mutex_t      mutex lock to ensure mutual exclusion
    status       int                  status of socket (0 -> free, 1 -> alloted)
    PID          int                  PID of process linked with the socket
    UDP_sockfd   int                  file descriptor of corresponding UDP socket
    rec_addr     struct sockaddr_in   socket address of receiver (destination)
    send_buff    buff                 send buffer
    swnd         window               sender window (for flow control)
    rec_buff     buff                 receive buffer
    rwnd         window               receiver window (for flow control)
    received     int[]                array marking which receiver buffer indices are filled
    last_sent    time_t               timestamp when last message-send happened
</code>
<code>
Functions:
I.  ksocket.c
    1. k_socket - function to create a ksocket
        Finds a free ksocket in shared memory, initializes its fields and creates corresponding UDP socket.
	input: domain, type (SOCK_KTP), protocol (0)
	output: returns file descriptor (index in shared memory) of ksocket linked to the process or -1 if error
		EINVALID error if arguments are invalid
		ESCKTFAIL error if shared memory access failed
		ENOSPACE error if all ksockets occupied
</code>
<code>
    2. k_bind - function to bind ksocket
        Binds corresponding UDP socket to source address and stores destination address.
	input: file descriptor of ksocket, source address, destination address
	output: returns 0 if bind successful or -1 if error
		EINVALID error if arguments are invalid
		ESCKTFAIL error if shared memory access failed
</code>
<code>
    3. k_sendto - function to send messages
        Checks for space in sender window and transfers message to send buffer.
	input: file descriptor of ksocket, buffer, size of buffer, flags, destination address, length of address
	output: returns size of message transferred if successful or -1 if error
		ESCKTFAIL error if shared memory access failed
		ENOTBOUND error if destination address doesn't match destination address bound to ksocket
		ENOSPACE error if no space in send buffer
</code>
<code>
    4. k_recvfrom - function to receive messages
        Checks if there are messages in receive buffer and retrieves them.
	input: file descriptor of ksocket, buffer, size of buffer, flags, pointers to structure for storing sender's address and length of the address
	output: returns size of message received if successful or -1 if error
		ESCKTFAIL error if shared memory access failed
		ENOMESSAGE error if no message in receiver buffer
</code>
<code>
    5. k_close - function to close ksocket
        Resets fields and closes corresponding UDP socket.
	input: file descriptor of ksocket
	output: returns 0 if socket close successful or -1 if error
		EINVALID error if arguments are invalid
		ESCKTFAIL error if shared memory access failed
</code>
<code>
    6. dropMessage - function to drop messages
        Returns whether to drop received message or not (1 or 0) based on probability p.
</code>
<code>
II. initksocket.c
    1. R - thread for receiving messages
        Selects from active ksockets to receive packets and responds to received packet depending on whether it's a duplicate or non-duplicate message or acknowledgement or a no-space query. 
</code>
<code>
    2. S - thread for sending messages
        Sends unsent messages or unacknowledged messages after timeout, or no-space query if last acknowledgement advertised zero window size.
</code>
<code>
    3. garbage_collector - function to clean up unclosed sockets
        Takes socket id of ksocket whose process was terminated and cleans its fields and closes associated UDP socket.
</code>
<code>
    4. getindexfromseq - helper function 
        Gets message buffer index from sequence number by comparing with start seq no and valid range of seq nos.
</code>
<code>
    5. sighandler - signal handler function 
        Modifies signal handling to remove shared memory on interrupt.
</code>
