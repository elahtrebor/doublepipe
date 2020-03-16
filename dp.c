#define _XOPEN_SOURCE 600
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#define __USE_BSD
#include <termios.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <string.h>
#include <ctype.h>

#define MAX_INPUT 1024
#define BINDIR "/usr/bin"
#define MAGIC 23       // Just kidding, I am out of magic today..
char array[2][MAX_INPUT];

#define DEBUG 0
int error(char *s){
 fprintf(stderr, "%s\n", s);
 return EXIT_FAILURE;
}

int nextchar(char *s){
 int c = 0;
 s++;
 c = *s++;
 s--;s--;
 return c;
}

void parsepipe(char *s){
 int i = 0;
 int x = 0; 
 while(*s != '\0'){
  if( *s == '\n') s++;
  if(*s=='|' && nextchar(s) == '|') { s+=2;i++;x=0; }
  array[i][x] = *s;
  s++; x++;
 }
 if(DEBUG) printf("DEBUG array[0]: '%s'\n", array[0]);
 if(DEBUG) printf("DEBUG array[1]: '%s'\n", array[1]);

}



int main(int argc, char *argv[]) {
int fdm, fds;
int rc;
char input[MAX_INPUT];
char *buffer;
int cmdflag = 0;

if (argc <= 1) {
  fprintf(stderr, "Incorrect arguments detected. Please use %s <PROGRAM_TO_SPAWN>\n", argv[0]);
  return EXIT_FAILURE;
}
  FILE *f1 = fopen(".dp_cmd", "w");
  fclose(f1);
  f1 = fopen(".dp_flag", "w");
  fprintf(f1, "%s\n", "NULL");
  fclose(f1);
 
fdm = posix_openpt(O_RDWR);                // OPEN THE PTY MASTER HANDLE
if (fdm < 0) error("posix_openpt ERROR\n");

rc = grantpt(fdm);                        // GRANT PRIVS TO THE MASTER PTY
if (rc != 0) error("grantpt ERROR\n");

rc = unlockpt(fdm);                       // UNLOCK THE MASTER 
if (rc != 0) error("unlockpt ERROR\n");

fds = open(ptsname(fdm), O_RDWR);         // Open the slave side ot the PTY

if (fork()) {
fd_set fd_in;
  close(fds);                            // Close the slave side of the PTY

  while (1) {
    // Wait for data from standard input and master side of PTY
    FD_ZERO(&fd_in);
    FD_SET(0, &fd_in);
    FD_SET(fdm, &fd_in);

    rc = select(fdm + 1, &fd_in, NULL, NULL, NULL);
    switch(rc) {
      case -1 : fprintf(stderr, "Error %d on select()\n", errno);
                exit(1);
      default :
      {
        // If data on standard input
        if (FD_ISSET(0, &fd_in)) {
             rc = read(0, input, sizeof(input));
            // parse the command input and strip anything after a double pipe
          char *cmdbuf = strdup(input);
          cmdbuf[rc] = '\0'; 

          if (rc > 0) {
            cmdflag = 0;
            // Send data on the master side of PTY
           if(strstr(cmdbuf,"||")){
          parsepipe(cmdbuf);
               memset(input, 0 , sizeof(input));
               strcpy(input, array[0]);
               rc = strlen(input + 1);
             FILE *fp = fopen(".dp_cmd", "w");
             fprintf(fp, "%s", array[1]);
             fclose(fp);

             fp = fopen(".dp_flag", "w");
             fprintf(fp, "%s", "EXEC");
             fclose(fp);
               cmdflag = 1;
            } else {

             FILE *fp = fopen(".dp_flag", "w");
             fprintf(fp, "%s", "NULL");
             fclose(fp);

            }
// THIS IS THE COMMAND INPUT THAT IS SENT TO THE FAR END  **********************************************           
            write(fdm, input, rc);   // THIS SENDS THE DATA TO THE FAR END
            if(cmdflag) { write(fdm, "\n", 1); }
          }
          else {
            if (rc < 0) {
              fprintf(stderr, "Error %d on read standard input\n", errno);
              exit(1);
            }
          }
        }

        // If data on master side of PTY
        if (FD_ISSET(fdm, &fd_in)) {
          char buff[MAX_INPUT];
          char cmdbuff[MAX_INPUT];

       // Check if we have an EXEC flag
          FILE *fp = fopen(".dp_flag", "r");
          fgets(buff, MAX_INPUT, fp);
          fclose(fp);

       // if we have an EXEC CMD then read the command
          fp = fopen(".dp_cmd", "r");
          fgets(cmdbuff, MAX_INPUT, fp);
          fclose(fp);
          rc = read(fdm, input, sizeof(input));
          if (rc > 0) {
            // THIS IS THE OUTPUT COMING BACK AFTER COMMAND IS SENT ***************************************************
      // IF we have an EXEC then PIPE the return output to the local shell             
    if(strstr(buff, "EXEC")){  
              FILE* file = popen(cmdbuff,"w");
              fwrite(input, sizeof(char), strlen(input), file);
              pclose(file);
               
           }  else {

                     write(1, input, rc); //
           }
         
              
          }
          else {
            if (rc < 0) {
              fprintf(stderr, "Error %d on read master PTY\n", errno);
              exit(1);
            }
          }
        }
      }
    } // End switch
  } // End while
}
else {
struct termios slave_orig_term_settings; // Saved terminal settings
struct termios new_term_settings; // Current terminal settings

  // CHILD

  // Close the master side of the PTY
  close(fdm);

  // Save the defaults parameters of the slave side of the PTY
  rc = tcgetattr(fds, &slave_orig_term_settings);

  // Set RAW mode on slave side of PTY
  new_term_settings = slave_orig_term_settings;
  cfmakeraw (&new_term_settings);
  tcsetattr (fds, TCSANOW, &new_term_settings);

  // The slave side of the PTY becomes the standard input and outputs of the child process
  close(0); // Close standard input (current terminal)
  close(1); // Close standard output (current terminal)
  close(2); // Close standard error (current terminal)

  dup(fds); // PTY becomes standard input (0)
  dup(fds); // PTY becomes standard output (1)
  dup(fds); // PTY becomes standard error (2)

  // Now the original file descriptor is useless
  close(fds);

  // Make the current process a new session leader
  setsid();

  // As the child is a session leader, set the controlling terminal to be the slave side of the PTY
  // (Mandatory for programs like the shell to make them manage correctly their outputs)
  ioctl(0, TIOCSCTTY, 1);

  // Execution of the program
  {
  char **child_av;
  int i;

    // Build the command line
    child_av = (char **)malloc(argc * sizeof(char *));
    for (i = 1; i < argc; i ++) {
      child_av[i - 1] = strdup(argv[i]);
    }
    child_av[i - 1] = NULL;
    rc = execvp(child_av[0], child_av);
  }

  // if Error...
  return 1;
}

return 0;
} // main
