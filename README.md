# doublepipe
A terminal utility that allows to pipe remote session output to local resources.
<br>
This code based on: http://rachid.koucha.free.fr/tech_corner/pty_pdip.html - examples for psuedo terminals.
<br>
<pre>

Compile with:  gcc -o dp dp.c 

Example:

 $./dp /bin/bash
 ssh myuser@router1
 Password: ********
 ROUTER1>sh tacacs||grep -i server
 Tacacs+ Server -  public  :
            Server address: 192.168.0.99
               Server port: 49
Tacacs+ Server -  public  :
            Server address: 192.168.0.100
               Server port: 49

        
ROUTER1>exit
$exit
</pre>
-----------------------------------------------------------------------------------------------------------------------
</p>
How does this work?  

The program spawns a psuedo terminal and parses the input sent from the user looking for a "double pipe".
If a double pipe is encountered the program splits the command at the double pipe and only sends the first 
command while saving the second command in a buffer. 
When the output of the first command is returned from the remote session, the program opens a local pipe to send 
the returned output to the previously saved buffer command. 

In the example above the command "show tacacs||grep -i server", the parser splits the input into two parts:
</p>
<pre>
array[0] = "show tacacs"
array[1] = "grep -i server"
</pre>
<p>
The first command "show tacacs" is sent to the far end and the output of that command is piped locally to "grep -i server".

This code is BETA release currently and needs to implement password hiding as well as more testing.

Need to work on the following:

- Psuedo terminal shows password.
- The terminal needs to interpret escape and control sequences.
- The program currently writes to local files to pass data from the parent process to the child. Need to implement shared memory.
- The parser needs its own object  IE: typedef struct parser {  .... }
- Checks for failure to open files.
- research as this could be re-developed using a legitimate terminal library versus the pseudo terminal as the PIPE code can be ported easily.

</p>

