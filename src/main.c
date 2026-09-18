#include "lab.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef TEST
#define main main_exclude
#endif
static int usage(FILE *out,int status){
 fprintf(out,"Usage: myapp -f <from> -t <to> [-s subject] [-b body] [-p port]\n"
 "          [-H helo-host] <server>\n\n"
 "  -f <from>       envelope sender, for example you@example.com\n"
 "  -t <to>         envelope recipient\n"
 "  -s <subject>    subject line (default: empty)\n"
 "  -b <body>       message body (default: read from stdin)\n"
 "  -p <port>       port or service name (default: 25)\n"
 "  -H <helo-host>  host name sent with HELO (default: localhost)\n"
 "  <server>        host name or address of the mail server\n");return status;
}
static char *read_body(void){
 /* Grow the input buffer as needed because stdin has no fixed message limit. */
 size_t used=0,cap=1024;char *p=malloc(cap);if(!p)return NULL;
 for(;;){if(used+1==cap){if(cap>(size_t)-1/2){free(p);return NULL;}cap*=2;char *q=realloc(p,cap);if(!q){free(p);return NULL;}p=q;}
  size_t n=fread(p+used,1,cap-used-1,stdin);used+=n;if(!n){if(ferror(stdin)){free(p);return NULL;}break;}}
 p[used]='\0';return p;
}
int main(int argc,char **argv){
 /* No arguments is a successful usage-display path required by make leak. */
 if(argc==1)return usage(stdout,0);
 const char *from=NULL,*to=NULL,*subject="",*body_arg=NULL,*port="25",*helo="localhost";int ch;opterr=0;
 while((ch=getopt(argc,argv,"f:t:s:b:p:H:"))!=-1)switch(ch){
  case'f':from=optarg;break;case't':to=optarg;break;case's':subject=optarg;break;
  case'b':body_arg=optarg;break;case'p':port=optarg;break;case'H':helo=optarg;break;
  default:return usage(stderr,1);
 }
 if(!from||!to||optind+1!=argc||smtp_has_newline(from)||smtp_has_newline(to)||smtp_has_newline(subject)||smtp_has_newline(helo)){
  fprintf(stderr,"Invalid command line or CR/LF in an SMTP field.\n");return usage(stderr,1);
 }
 /* A -b value is borrowed from argv; stdin input is owned and freed below. */
 char *owned=NULL;const char *body=body_arg;if(!body){owned=read_body();if(!owned){fprintf(stderr,"Could not read message body from stdin.\n");return 2;}body=owned;}
 char *error=NULL;int fd=smtp_socket_connect(argv[optind],port,&error);
 if(fd<0){fprintf(stderr,"Could not connect to %s:%s: %s\n",argv[optind],port,error?error:"unknown error");free(error);free(owned);return 2;}
 smtp_transport t={smtp_socket_read,smtp_socket_write,&fd};int result=smtp_run_session(t,helo,from,to,subject,body,&error);
 close(fd);free(owned);if(result){fprintf(stderr,"%s\n",error?error:"SMTP session failed");free(error);return 2;}free(error);return 0;
}
