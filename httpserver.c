#include "asgn2_helper_funcs.h"
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <linux/limits.h>
#include <string.h>
#include <fcntl.h>
#include <regex.h>
#include <errno.h>
#include <sys/socket.h>

#define ARRAY_SIZE(arr) (sizeof((arr)) / sizeof((arr)[0]))

static const char *const re = "^([A-Z]{1,8}) +/([a-zA-Z0-9._]{1,63}) "
                              "+(HTTP/([0-9])\\.([0-9]))\r\n((([a-zA-Z0-9.-]{1,128}): "
                              "+(.{0,128})\r\n)*)\r\n(.*)";

static const char *const contentre = "(([a-zA-Z0-9.-]{1,128}): ([a-zA-Z0-9]{0,128})\r\n)";

extern int errno;

//function for handling the get stuff
void get(char *filename, int dst) {
    char *response;
    struct stat fileStat;
    //checks if file doesn't exist
    if (stat(filename, &fileStat) == -1 && errno == ENOENT) {
        response = "HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n";
        write_n_bytes(dst, response, strlen(response));
        return;
    }
    //checks if the filename is a directory or not
    if (S_ISDIR(fileStat.st_mode)) {

        response = "HTTP/1.1 403 Forbidden\r\nContent-Length: 12\r\n\r\nForbidden\n";
        write_n_bytes(dst, response, strlen(response));
        fprintf(stderr, "test\n");
        return;
    }

    int fd = open(filename, O_RDONLY, 0);
    if (fd == -1 && errno == ENOENT) {
        response = "HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n";
        write_n_bytes(dst, response, strlen(response));
        return;
    } else if (fd == -1 && (errno == EACCES || errno == EISDIR)) {
        response = "HTTP/1.1 403 Forbidden\r\nContent-Length: 12\r\n\r\nForbidden\n";
        write_n_bytes(dst, response, strlen(response));
        return;
    } else {
        char *conLen = malloc(10 * sizeof(char));
        sprintf(conLen, "%ld", fileStat.st_size);
        response = "HTTP/1.1 200 OK\r\nContent-Length: ";
        write_n_bytes(dst, response, strlen(response));
        write_n_bytes(dst, conLen, strlen(conLen));
        response = "\r\n\r\n";
        free(conLen);
        int wri = write_n_bytes(dst, response, strlen(response));
        while (wri != 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            wri = pass_n_bytes(fd, dst, PATH_MAX);
        }
    }
    close(fd);
}
//might use this again for modularity ngl
/*
void put(char *filename, int accept, int length, char *initWrite, int initLen) {
    char *response;
    int status_code = 200;
    int fd = open(filename, O_WRONLY | O_TRUNC, 0666);
    if (fd == -1 && errno == ENOENT) {
        fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        status_code = 201;
    }
    int wri = write_n_bytes(fd, initWrite, initLen);

    //int writing = 1;
    if (wri < length) {
        wri += pass_n_bytes(accept, fd, length - wri);
    }
    close(fd);
    if (wri != length) {
        fprintf(stderr, "Length Written - %d\n", wri);
        fprintf(stderr, "Length Needed - %d\n", length);
        fprintf(stderr, "%s\n", strerror(errno));
        if (status_code == 200) {
            fd = open(filename, O_WRONLY | O_TRUNC, 0666);
            close(fd);
        } else if (status_code == 201) {
            unlink(filename);
        }
        response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n";
        write_n_bytes(accept, response, strlen(response));
        return;
    }
    if (status_code == 200) {
        response = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nOK\n";
        write_n_bytes(accept, response, strlen(response));
    } else if (status_code == 201) {
        response = "HTTP/1.1 201 OK\r\nContent-Length: 8\r\n\r\nCreated\n";
        write_n_bytes(accept, response, strlen(response));
    }
}
*/

int main(int argc, char *argv[]) {
    // standard input checks
    if (argc != 2) {
        exit(1);
    }
    int port = atoi(argv[1]);
    if (port < 1 || port > 65535) {
        exit(1);
    }
    //initializing socket
    Listener_Socket sock;
    int a = listener_init(&sock, port);
    if (a == -1) {
        exit(1);
    }

    // getting input to parse
    char buf[PATH_MAX];
    char *response;
    //compiling first set of regex

    while (1) {
        //setting up the regex in here cuz I free it at the end of the loop
        regex_t preg;
        regmatch_t pmatch[50];
        int comp = regcomp(&preg, re, REG_EXTENDED | REG_NEWLINE);
        if (comp != 0) {
            response = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal "
                       "Server Error\n";
            //let's hope this error doesn't happen?
            write_n_bytes(1, response, strlen(response));
        }

        //compiling regex for headers
        regex_t contentPreg;
        regmatch_t cpMatch[50];
        int contentComp = regcomp(&contentPreg, contentre, REG_EXTENDED | REG_NEWLINE);
        if (contentComp != 0) {
            response = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal "
                       "Server Error\n";
            //let's hope this error doesn't happen?
            write_n_bytes(1, response, strlen(response));
        }

        //clearing buffer and accepting port
        memset(buf, 0, sizeof(buf));
        int accept = listener_accept(&sock);

        if (accept == -1) {
            response = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal "
                       "Server Error\n";
            //let's hope this error doesn't happen?
            write_n_bytes(1, response, strlen(response));
        }
        //reading in buffer
        int res = read_until(accept, buf, 2048, "\r\n\r\n");

        // maybe wrong way of going about it?
        if (res == -1 && errno != ETIME) {
            // printf("%d\n", errno);
            // printf("%s\n", strerror(errno));
            response = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 22\r\n\r\nInternal "
                       "Server Error\n";
            write_n_bytes(accept, response, strlen(response));
            close(accept);
        }
        //finding regex matches and initializing other values
        char *s = buf;
        int length = 0;
        int getInt = 0, putInt = 0;
        comp = regexec(&preg, buf, ARRAY_SIZE(pmatch), pmatch, 0);
        if (comp != 0) {
            response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n";
            write_n_bytes(accept, response, strlen(response));
        } else {
            //checking that the first match is either GET or PUT
            //throwing not implemented return otherwise
            if (strncmp(s + pmatch[1].rm_so, "GET", 3) == 0) {
                getInt = 1;
            } else if (strncmp(s + pmatch[1].rm_so, "PUT", 3) == 0) {
                putInt = 1;
            } else {
                response = "HTTP/1.1 501 Not Implemented\r\nContent-Length: 16\r\n\r\nNot "
                           "Implemented\n";
                write_n_bytes(accept, response, strlen(response));
            }
            //checking that in the HTTP/1.1 HAS ACTUALLY ONLY 1 AND 1 IN IT
            //because 4th and 5th matches should be the numbers in HTTP/#.#
            if (strncmp(s + pmatch[4].rm_so, "1", 1) != 0
                || strncmp(s + pmatch[5].rm_so, "1", 1) != 0) {
                response = "HTTP/1.1 505 Version Not Supported\r\nContent-Length: "
                           "22\r\n\r\nVersion Not Supported\n";
                write_n_bytes(accept, response, strlen(response));
                getInt = 0;
                putInt = 0;
            }
            int uri_length = pmatch[2].rm_eo - pmatch[2].rm_so;
            char filename[uri_length + 1];
            strncpy(filename, s + pmatch[2].rm_so, uri_length);
            filename[uri_length] = '\0';
            //now if it was a get, make sure that it performs the logic for get
            if (getInt == 1) {
                //making sure there's no content after the \r\n\r\n (10th match)
                if ((pmatch[10].rm_eo - pmatch[10].rm_so) == 0) {
                    get(filename, accept);
                } else {
                    response
                        = "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n";
                    write_n_bytes(accept, response, strlen(response));
                }
                memset(buf, 0, sizeof(buf));
            }
            if (putInt == 1) {
                //getting the headers to find the content length
                int header_len = pmatch[6].rm_eo - pmatch[6].rm_so;
                char headers[header_len + 1];
                strncpy(headers, s + pmatch[6].rm_so, header_len);
                headers[header_len] = '\0';

                char *sCon = headers;
                contentComp = regexec(&contentPreg, headers, ARRAY_SIZE(cpMatch), cpMatch, 0);
                //this SHOULD never happen because it's legit a subset of the other regex, but just in case
                if (contentComp != 0) {
                    response
                        = "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n";
                    write_n_bytes(accept, response, strlen(response));

                } else {

                    //loop through all the cases just to find the one with content length
                    for (int k = 1; k < (int) ARRAY_SIZE(cpMatch); ++k) {
                        int header_len = cpMatch[k].rm_eo - cpMatch[k].rm_so;
                        if (strncmp(sCon + cpMatch[k].rm_so, "Content-Length", header_len) == 0) {
                            char lenCon[cpMatch[k + 1].rm_eo - cpMatch[k + 1].rm_so + 1];
                            strncpy(lenCon, sCon + cpMatch[k + 1].rm_so,
                                cpMatch[k + 1].rm_eo - cpMatch[k + 1].rm_so);

                            lenCon[cpMatch[k + 1].rm_eo - cpMatch[k + 1].rm_so] = '\0';

                            length = atoi(lenCon);
                            break;
                        }
                    }

                    int status_code = 200;
                    int fd = open(filename, O_WRONLY | O_TRUNC, 0666);
                    if (fd == -1 && errno == ENOENT) {

                        fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                        status_code = 201;
                        if (fd == -1 && errno == EACCES) {
                            close(accept);
                            response
                                = "HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n";
                            write_n_bytes(accept, response, strlen(response));
                            continue;
                        }
                    } else {
                        status_code = 200;
                    }
                    // fprintf(stderr, "Position Ending Message - %d\n", pmatch[10].rm_eo);
                    // fprintf(stderr, "Position Beginning Message - %d\n", pmatch[10].rm_so);
                    // fprintf(stderr, "difference in lens = %d --> %d\n", length,
                    //     pmatch[10].rm_eo - pmatch[10].rm_so);

                    int written = write_n_bytes(fd, s + pmatch[10].rm_so, res - pmatch[10].rm_so);
                    // fprintf(stderr, "Ending Of Buffer Read - %d\n", res);
                    // fprintf(stderr, "Written Before Loop - %d\n", written);

                    while (written < length) {
                        written += pass_n_bytes(accept, fd, length - written);
                    }
                    close(fd);

                    if (written != length) {
                        // fprintf(stderr, "Length Written - %d\n", written);
                        // fprintf(stderr, "Length Needed - %d\n", length);
                        // fprintf(stderr, "%s\n", strerror(errno));
                        if (status_code == 200) {
                            fd = open(filename, O_WRONLY | O_TRUNC, 0666);
                            close(fd);
                            // } else if (status_code == 201) {
                            //     unlink(filename);
                        }
                        response
                            = "HTTP/1.1 400 Bad Request\r\nContent-Length: 12\r\n\r\nBad Request\n";
                        write_n_bytes(accept, response, strlen(response));
                    } else if (errno == EAGAIN && status_code != 200 && status_code != 201) {
                        fprintf(stderr, "What is the status: %d\n", status_code);
                        response = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: "
                                   "22\r\n\r\nInternal "
                                   "Server Error\n";
                        write_n_bytes(accept, response, strlen(response));
                    } else if (status_code == 200) {
                        response = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nOK\n";
                        write_n_bytes(accept, response, strlen(response));
                    } else if (status_code == 201) {
                        response = "HTTP/1.1 201 Created\r\nContent-Length: 8\r\n\r\nCreated\n";
                        write_n_bytes(accept, response, strlen(response));
                    }
                }
                regfree(&contentPreg);
                memset(buf, 0, sizeof(buf));
            }
        }

        close(accept);
        regfree(&preg);
    }
}
