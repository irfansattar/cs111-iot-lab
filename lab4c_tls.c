#include <stdlib.h>
#include <getopt.h>
#include <mraa.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <math.h>
#include <errno.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#define LAB "lab4b"
int DEBUG = 0;

// Global variables
SSL *ssl;
int SOCK;
char *scale = NULL;
int period = 1;
int startFlag = 1;

float cToF(float c)
{
    return (c * (9.0/5.0)) + 32;
}

float readTemp(const mraa_aio_context sensor, const char * scale)
{
    // Constants
    int R0 = 100000;
    int B = 4275;

    int reading = mraa_aio_read(sensor);

    float R = 1023.0 / reading - 1.0;
    R *= R0;

    float temp = 1.0/(log(R/R0)/B+1/298.15)-273.15;;

    if (strcmp(scale, "F") == 0)
    {
        temp = cToF(temp);
    }
    
    return temp;
}

// OLD BUTTON CODE NOT PART OF THIS LAB
// void* buttonHandler(void *fd)
// {
//     int logFd = *(int *) fd;

//     time_t rawTime;
//     time(&rawTime);
//     struct tm *formattedTime_curr = localtime(&rawTime);
    
//     printf("%02d:%02d:%02d SHUTDOWN\n", formattedTime_curr->tm_hour, formattedTime_curr->tm_min, formattedTime_curr->tm_sec);

//     if (logFd != -2)
//     {
//         char shutdownOutput[128];
//         sprintf(shutdownOutput, "%02d:%02d:%02d SHUTDOWN\n", formattedTime_curr->tm_hour, formattedTime_curr->tm_min, formattedTime_curr->tm_sec);
//         write(logFd, &shutdownOutput, strlen(shutdownOutput));
//     }

//     exit(0);
// }

void processCmd(char *cmd, int logFd)
{
    if (DEBUG)
    {
        fprintf(stderr, "%s| cmd: %s", LAB, cmd);
    }

    if (logFd != -2)
    {
        write(logFd, cmd, strlen(cmd));
    }

    if (strstr(cmd, "SCALE=") != NULL)
    {
        if (cmd[6] == 'F')
        {
            scale = "F";
        }
        else if (cmd[6] == 'C')
        {
            scale = "C";
        }
        
        if (DEBUG)
        {
            fprintf(stderr, "%s| SCALE cmd, new val:%s original string:%s\n", LAB, scale, cmd);
        }
    }
    else if (strstr(cmd, "PERIOD=") != NULL)
    {
        period = atoi(&cmd[7]);

        if (DEBUG)
        {
            fprintf(stderr, "%s| period changed to %d, original string %s\n", LAB, period, cmd);
        }
    }
    else if (strstr(cmd, "STOP") != NULL)
    {
        startFlag = 0;

        if (DEBUG)
        {
            fprintf(stderr, "%s| STOP cmd, new val:%d original string:%s\n", LAB, startFlag, cmd);
        }
    }
    else if (strstr(cmd, "START") != NULL)
    {
        startFlag = 1;

        if (DEBUG)
        {
            fprintf(stderr, "%s| START cmd, new val:%d original string:%s\n", LAB, startFlag, cmd);
        }
    }
    else if (strstr(cmd, "OFF") != NULL)
    {
        time_t rawTime;
        time(&rawTime);
        struct tm *formattedTime_curr = localtime(&rawTime);
        
        char shutdownOutput[128];
        sprintf(shutdownOutput, "%02d:%02d:%02d SHUTDOWN\n", formattedTime_curr->tm_hour, formattedTime_curr->tm_min, formattedTime_curr->tm_sec);
        if (SSL_write(ssl, &shutdownOutput, strlen(shutdownOutput)) <= 0)
        {
            fprintf(stderr, "%s| error writing to socket (error code: %d), exiting...\n", LAB, errno);
            exit(2);
        }

        if (logFd != -2)
        {
            write(logFd, &shutdownOutput, strlen(shutdownOutput));
        }

        exit(0);
    }
    // Any incoming cmd is first logged and LOG cmds only need to be logged so do nothing
    else if (strstr(cmd, "LOG") != NULL)
    {
        ;
    }
    // Bogus command, already logged so do nothing (no echo to stdout)
    else
    {
        ;
    }
}

void printCmd(int cmdStartIndex, int newlineIndex, char *buf)
{
    fprintf(stderr, "%s| printing cmd being sent to processCmd: ", LAB);

    for (int i = cmdStartIndex; i < newlineIndex; i++)
    {
        fprintf(stderr, "%c", buf[i]);
    }

    fprintf(stderr, " ... end of cmd\n");
}

void processInput(int logFd)
{
    int CMDBUFMAXSIZE = 1024, RDBUFMAXSIZE = 512, cmdBufSize = CMDBUFMAXSIZE, cmdBufIndex = 0, rdBufIndex, rdcnt, done = 0, readFlag = 1;
    char cmdBuf[sizeof(char) * (CMDBUFMAXSIZE + 1)]; // +1 safety for '\0', reading up to 512 actual chars
    char *rdBuf = NULL;

    // Read from read()
    // process char by char until newline, end of read buf, end of cmdBuf
    // if newline, send to processCmd
    // if end of buf, read again and continue adding to cmdBuf until above conditions met

    while (!done)
    {
        if (readFlag)
        {
            rdBuf = malloc(sizeof(char) * (RDBUFMAXSIZE + 1));

            rdBuf[RDBUFMAXSIZE + 1] = '\0';
            rdcnt = SSL_read(ssl, rdBuf, RDBUFMAXSIZE);
            rdBufIndex = 0;
            readFlag = 0;
        }

        if (DEBUG)
        {
            fprintf(stderr, "%s| read from SSL socket: %s ... End of read\n", LAB, rdBuf);
        }

        if (rdcnt <= 0)
        {
            fprintf(stderr, "%s| error reading from SSL socket (error code: %d -> %s), exiting...\n", LAB, errno, strerror(errno));
            exit(2);
        }

        while (rdBufIndex < rdcnt && rdBuf[rdBufIndex] != '\n' && cmdBufSize > 0)
        {
            cmdBuf[cmdBufIndex] = rdBuf[rdBufIndex];
            cmdBufIndex++;
            rdBufIndex++;
            cmdBufSize--;
        }

        // If current char is '\n' (we're at end of cmd) && next index is out of bounds, 
        // we've read entire cmd and (assume) nothing more to read after processing this cmd -> done processing, set done flag
        if (rdBuf[rdBufIndex] == '\n' && rdBufIndex + 1 >= rdcnt)
        {
            done = 1;
        }
        
        if (rdBuf[rdBufIndex] == '\n')
        {
            // processCmd, reset indices/sizes, do not reset read flag b/c there may be more to read in rdBuf, continue statement
            cmdBuf[cmdBufIndex++] = rdBuf[rdBufIndex]; // Copy over newline char
            cmdBuf[cmdBufIndex] = '\0'; // Null terminate cmd string
            processCmd(cmdBuf, logFd);
            cmdBufSize = CMDBUFMAXSIZE; // Reset size of cmdBuf, since cmd is now processed we can use full buf for next cmd
            cmdBufIndex = 0; // Reset cmdBufIndex b/c we are writing new cmd to buf so start at beginning of buf
            rdBufIndex++; // Increment rdBufIndex to start of next command
            // continue;
        }
        else if (rdBufIndex >= rdcnt)
        {
            // read everything from read call but at partial cmd or LOG cmd w/ long text
            // reset indices/sizes (no need actually, done in read if statement above),
            //   free old rdBuf, reset read flag, continue statement to read again and continue processing
            free(rdBuf);
            readFlag = 1; // Reset read flag so we read again at next iteration
            // continue;
        }
        else if (cmdBufSize <= 0) // Most likely in case of a long LOG text cmd (not fully sure tho) so we just process cmd and start again
        {
            // cmdBuf full, process cmd, reset, continue statement
            cmdBuf[cmdBufIndex] = '\0'; // Set +1 overflow spot from earlier to null terminate string
            processCmd(cmdBuf, logFd);
            cmdBufSize = CMDBUFMAXSIZE; // Reset size of cmdBuf, since cmd is now processed we can use full buf to continue processing
            cmdBufIndex = 0; // Reset cmdBufIndex b/c we continue processing with 'clean' buf so start at beginning of buf
            // continue;
        }
    }
}

// Difference in time in seconds
int timeDiff(const struct tm oldTime, const struct tm newTime)
{
    return ((newTime.tm_hour - oldTime.tm_hour) * 3600) + ((newTime.tm_min - oldTime.tm_min) * 60) + (newTime.tm_sec - oldTime.tm_sec);
}

int main(int argc, char * const argv[])
{
    // Arg processing
    int opt;
    char *logFN = NULL, *host = NULL, *id = NULL, *port = NULL;

    struct option long_options[] =
    {
        {"period", required_argument, NULL, 'p'},
        {"scale", required_argument, NULL, 's'},
        {"log", required_argument, NULL, 'l'},
        {"id", required_argument, NULL, 'i'},
        {"host", required_argument, NULL, 'h'},
        {"debug", no_argument, &DEBUG, 1},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, ":p:s:l:di:h:", long_options, NULL)) != -1)
    {
        switch (opt)
        {
        case 'p':
            period = atoi(optarg);
            break;
        case 's':
            scale = optarg;
            break;
        case 'l':
            logFN = optarg;
            break;
        case 'i':
            id = optarg;
            break;
        case 'h':
            host = optarg;
            break;
        case 'd':
            DEBUG = 1;
            break;
        case 0:
            break;
        case ':':
            fprintf(stderr, "%s| option -%c has required argument, see usage below; exiting...\n usage: %s [--period=<time in seconds>] [--scale=<C or F>] [--log=<filename>] --host=<hostname> --id=<ID> <port>\n", LAB, optopt, LAB);
            exit(1);
        case '?':
        default:
            fprintf(stderr, "%s| option -%c is not allowed, see usage below; exiting...\n usage: %s [--period=<time in seconds>] [--scale=<C or F>] [--log=<filename>] --host=<hostname> --id=<ID> <port>\n", LAB, optopt, LAB);
            exit(1);
        }
    }

    if (host == NULL)
    {
        fprintf(stderr, "%s| Required host argument option missing, see usage below; exiting...\n usage: %s [--period=<time in seconds>] [--scale=<C or F>] [--log=<filename>] --host=<hostname> --id=<ID> <port>\n", LAB, LAB);
        exit(1);
    }
    
    if (id == NULL)
    {
        fprintf(stderr, "%s| Required id argument option missing, see usage below; exiting...\n usage: %s [--period=<time in seconds>] [--scale=<C or F>] [--log=<filename>] --host=<hostname> --id=<ID> <port>\n", LAB, LAB);
        exit(1);
    }
    
    if (optind >= argc)
    {
        fprintf(stderr, "%s| Required port number argument option missing, see usage below; exiting...\n usage: %s [--period=<time in seconds>] [--scale=<C or F>] [--log=<filename>] --host=<hostname> --id=<ID> <port>\n", LAB, LAB);
        exit(1);
    }

    port = argv[optind++];

    if (optind < argc)
    {
        fprintf(stderr, "%s| Extra argument(s):", LAB);
        
        for (; optind < argc; optind++)
        {
            fprintf(stderr, " %s", argv[optind]);
        }

        fprintf(stderr, ", see usage below; exiting...\n usage: %s [--period=<time in seconds>] [--scale=<C or F>] [--log=<filename>] --host=<hostname> --id=<ID> <port>\n", LAB);
        exit(1);
    }

    if (scale != NULL && ((strcmp(scale, "C") != 0) && (strcmp(scale, "F") != 0)))
    {
        fprintf(stderr, "%s| --scale option takes only 'C' or 'F'\n usage: %s [--period=<time in seconds>] [--scale=<C or F>] [--log=<filename>] --host=<hostname> --id=<ID> <port>\n", LAB, LAB);
        exit(1);
    }
    else if (scale == NULL)
    {
        scale = "F";
    }

    if (DEBUG)
    {
        fprintf(stderr, "%s| Parsed input - period: %d, scale: %s, log: %s, hostname: %s, port: %s, id: %s\n", LAB, period, scale, logFN, host, port, id);
    }

    int logFd = -2;

    if (logFN != NULL && ((logFd = open(logFN, O_WRONLY|O_CREAT|O_TRUNC, 0666)) == -1))
    {
        fprintf(stderr, "%s| error opening log file (error code: %d -> %s), exiting...\n", LAB, errno, strerror(errno));
        exit(1);
    }    

    mraa_aio_context tempSensor;
    if ((tempSensor = mraa_aio_init(1)) == NULL)
    {
        fprintf(stderr, "%s| error initializing temperator sensor, exiting...\n", LAB);
        exit(2);
    }

    // ====== OLD BUTTON CODE NOT PART OF THIS LAB ======
    // mraa_gpio_context button;
    // if ((button = mraa_gpio_init(60)) == NULL)
    // {
    //     fprintf(stderr, "%s| error initializing button, exiting...\n", LAB);
    //     exit(1);
    // }

    // if ((mraa_rc = mraa_gpio_dir(button, MRAA_GPIO_IN)) != MRAA_SUCCESS)
    // {
    //     fprintf(stderr, "%s| error setting direction of button (mraa_result_t error code: %d), exiting...\n", LAB, mraa_rc);
    //     exit(1);
    // }

    // if ((mraa_rc = mraa_gpio_isr(button, MRAA_GPIO_EDGE_RISING, (void *) buttonHandler, &logFd)) != MRAA_SUCCESS)
    // {
    //     fprintf(stderr, "%s| error setting direction of button (mraa_result_t error code: %d), exiting...\n", LAB, mraa_rc);
    //     exit(1);
    // }
    // ==================================================

    // Set up SSL
    const SSL_METHOD *method;
    SSL_CTX *context;
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    if ((method = TLSv1_client_method()) == NULL)
    {
        fprintf(stderr, "%s| error initializing SSL method, exiting...\n", LAB);
        exit(2);
    }

    if ((context = SSL_CTX_new(method)) == NULL)
    {
        fprintf(stderr, "%s| error initializing SSL context, exiting...\n", LAB);
        exit(2);
    }

    if ((ssl = SSL_new(context)) == NULL)
    {
        fprintf(stderr, "%s| error initializing SSL, exiting...\n", LAB);
        exit(2);
    }

    if ((SOCK = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        fprintf(stderr, "%s| error creating socket (error code: %d), exiting...\n", LAB, errno);
        exit(2);
    }

    struct hostent *servaddr = gethostbyname(host);
    if (servaddr == NULL)
    {
        fprintf(stderr, "%s| error calling 'gethostbyname' (error code: %d), exiting...\n", LAB, errno);
        exit(1);
    }

    struct sockaddr_in servinfo;
    bzero((char *) &(servinfo), sizeof(servinfo));
    servinfo.sin_family = AF_INET;
    servinfo.sin_port = htons(atoi(port));
    servinfo.sin_addr.s_addr = *(long *) servaddr->h_addr_list[0];

    if (connect(SOCK,(struct sockaddr *) &servinfo, sizeof(servinfo)) == -1)
    {
        fprintf(stderr, "%s| error connecting socket (error code: %d), exiting...\n", LAB, errno);
        exit(2);
    }

    SSL_set_fd(ssl, SOCK);
    if (SSL_connect(ssl) != 1)
    {
        fprintf(stderr, "%s| error with SSL connection (error code: %d), exiting...\n", LAB, errno);
        exit(2);
    }

    char concatId[50];
	sprintf(concatId, "ID=%s\n", id);

    if (SSL_write(ssl, concatId, strlen(concatId)) <= 0)
    {
        fprintf(stderr, "%s| error writing to SSL socket (error code: %d), exiting...\n", LAB, errno);
        exit(2);
    }
	
    if (logFd > 0)
    {
        write(logFd, concatId, strlen(concatId));
    }
    
    if (DEBUG)
    {
        fprintf(stderr, "%s| connected and ID should be sent; id: %s\n", LAB, concatId);
    }

    time_t rawTime;
    time(&rawTime);
    struct tm formattedTime_last = *localtime(&rawTime);

    struct pollfd pollList[1];
    // 0 index is keyboard
    pollList[0].fd = SOCK;
    pollList[0].events = POLLIN;

    while (1)
    {
        int rc = poll(pollList, 1, 0);

        if (rc == -1)
        {
            fprintf(stderr, "%s| error polling SSL socket (error code: %d => %s), exiting...\n", LAB, errno, strerror(errno));
            exit(2);
        }

        if (rc > 0)
        {
            if (pollList[0].revents & POLLIN)
            {
                processInput(logFd);
            }
            else if (pollList[0].revents == POLLHUP || pollList[0].revents == POLLERR)
            {
                if (DEBUG)
                {
                    fprintf(stderr, "%s| Entered POLLHUP/POLLERR condition\r\n", LAB);
                }
                
                if (logFd > 0 && close(logFd) == -1)
                {
                    fprintf(stderr, "%s| error closing log file (error code: %d => %s), exiting...\n", LAB, errno, strerror(errno));
                    exit(2);
                }

                SSL_shutdown(ssl);
                SSL_free(ssl);
                
                exit(0);
            }
        }

        time_t rawTime;
        time(&rawTime);
        struct tm formattedTime_curr = *localtime(&rawTime);

        if (DEBUG)
        {
            // fprintf(stderr, "%s| last: h:%d m:%d s:%d  curr: h:%d m:%d s:%d\n", LAB, formattedTime_last.tm_hour, formattedTime_last.tm_min, formattedTime_last.tm_sec, formattedTime_curr.tm_hour, formattedTime_curr.tm_min, formattedTime_curr.tm_sec);
        }

        if (timeDiff(formattedTime_last, formattedTime_curr) >= period && startFlag)
        {
            formattedTime_last = formattedTime_curr;
            float temp = readTemp(tempSensor, scale);

            char tempOutput[128];
            sprintf(tempOutput, "%02d:%02d:%02d %0.1f\n", formattedTime_curr.tm_hour, formattedTime_curr.tm_min, formattedTime_curr.tm_sec, temp);
            if (SSL_write(ssl, &tempOutput, strlen(tempOutput)) <= 0)
            {
                fprintf(stderr, "%s| error writing to SSL socket (error code: %d), exiting...\n", LAB, errno);
                exit(2);
            }

            if (logFN != NULL)
            {
                write(logFd, &tempOutput, strlen(tempOutput));
            }
        }
    }
}