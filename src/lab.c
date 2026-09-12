#define _POSIX_C_SOURCE 200112L
#include "lab.h"
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static char *dupstr(const char *s) {
  size_t n = strlen(s)+1; char *p=malloc(n); if(p) memcpy(p,s,n); return p;
}
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
static char *fmt(const char *f, const char *a, const char *b, const char *c, const char *d) {
  int n=snprintf(NULL,0,f,a,b,c,d); if(n<0) return NULL;
  char *p=malloc((size_t)n+1); if(p) (void)snprintf(p,(size_t)n+1,f,a,b,c,d); return p;
}
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
int smtp_has_newline(const char *s) { return s && (strchr(s,'\r') || strchr(s,'\n')); }
int smtp_reply_code(const char *s) {
  if(!s || s[0]<'0'||s[0]>'9'||s[1]<'0'||s[1]>'9'||s[2]<'0'||s[2]>'9' ||
     (s[3]!=' '&&s[3]!='-'&&s[3]!='\r'&&s[3]!='\n'&&s[3]!='\0')) return -1;
  return (s[0]-'0')*100+(s[1]-'0')*10+s[2]-'0';
}
int smtp_reply_is_final(const char *s) { return smtp_reply_code(s)>=0 && s[3]!='-'; }
char *smtp_command(const char *v,const char *a) {
  if(!v||smtp_has_newline(v)||smtp_has_newline(a)) return NULL;
  return a&&*a ? fmt("%s %s\r\n",v,a,"","") : fmt("%s\r\n",v,"","","");
}
char *smtp_dot_stuff(const char *body) {
  if(!body) body="";
  size_t n=strlen(body), dots=0; int bol=1;
  for(size_t i=0;i<n;i++){ if(bol&&body[i]=='.') dots++; bol=body[i]=='\n'||body[i]=='\r'; }
  if(n>((size_t)-1-dots-1)/2) return NULL;
  char *out=malloc(n*2+dots+1); if(!out) return NULL;
  size_t j=0; bol=1;
  for(size_t i=0;i<n;i++){
    if(bol&&body[i]=='.') out[j++]='.';
    if(body[i]=='\r'&&i+1<n&&body[i+1]=='\n'){out[j++]='\r';out[j++]='\n';i++;bol=1;}
    else if(body[i]=='\n'||body[i]=='\r'){out[j++]='\r';out[j++]='\n';bol=1;}
    else {out[j++]=body[i];bol=0;}
  }
  out[j]='\0'; return out;
}
char *smtp_data_payload(const char *from,const char *to,const char *subject,const char *body) {
  if(!from||!to||!subject||smtp_has_newline(from)||smtp_has_newline(to)||smtp_has_newline(subject)) return NULL;
  char *stuffed=smtp_dot_stuff(body); if(!stuffed) return NULL;
  size_t n=strlen(stuffed); const char *sep=(n>=2&&stuffed[n-2]=='\r'&&stuffed[n-1]=='\n')?"":"\r\n";
  char *p=fmt("From: %s\r\nTo: %s\r\nSubject: %s\r\n\r\n%s",from,to,subject,stuffed);
  free(stuffed); if(!p) return NULL;
  size_t base=strlen(p), sn=strlen(sep); char *q=realloc(p,base+sn+4);
  if(!q){free(p);return NULL;} memcpy(q+base,sep,sn); memcpy(q+base+sn,".\r\n",4); return q;
}
int smtp_reader_init(smtp_reader *r,smtp_transport t){
  if(!r || !t.read || !t.write) return -1;
  r->transport=t;r->start=r->end=0;return 0;
}
int smtp_read_line(smtp_reader *r,char **line) {
  if(!r||!line||!r->transport.read) return -1;
  *line=NULL;
  for(;;){
    for(size_t i=r->start;i+1<r->end;i++) if(r->buffer[i]=='\r'&&r->buffer[i+1]=='\n'){
      size_t n=i+2-r->start; char *p=malloc(n+1); if(!p)return -1;
      memcpy(p,r->buffer+r->start,n);p[n]='\0';r->start=i+2;*line=p;return 0;
    }
    if(r->start){memmove(r->buffer,r->buffer+r->start,r->end-r->start);r->end-=r->start;r->start=0;}
    if(r->end==sizeof r->buffer)return -1;
    ssize_t n=r->transport.read(r->transport.context,r->buffer+r->end,sizeof r->buffer-r->end);
    if(n<=0||(size_t)n>sizeof r->buffer-r->end)return -1;
    r->end+=(size_t)n;
  }
}
int smtp_read_reply(smtp_reader *r,int *code,char **reply) {
  if(!code||!reply)return -1;
  *reply=NULL;size_t used=0;int first=-1;
  for(;;){char *line=NULL;if(smtp_read_line(r,&line)){free(*reply);*reply=NULL;return -1;}
    int cur=smtp_reply_code(line);if(cur<0||(first>=0&&cur!=first)){free(line);free(*reply);*reply=NULL;return -1;}
    if(first<0)first=cur;
    size_t add=strlen(line);char *p=realloc(*reply,used+add+1);
    if(!p){free(line);free(*reply);*reply=NULL;return -1;}*reply=p;memcpy(*reply+used,line,add+1);used+=add;
    int final=smtp_reply_is_final(line);free(line);if(final){*code=first;return 0;}
  }
}
int smtp_write_all(smtp_transport *t,const char *data,size_t len) {
  if(!t||!t->write||!data)return -1;
  size_t sent=0;
  while(sent<len){ssize_t n=t->write(t->context,data+sent,len-sent);if(n<=0||(size_t)n>len-sent)return -1;sent+=(size_t)n;}return 0;
}
int smtp_send_command(smtp_reader *r,const char *cmd,int expected,char **reply) {
  if(smtp_write_all(&r->transport,cmd,strlen(cmd)))return -1;
  int code;
  if(smtp_read_reply(r,&code,reply))return -1;
  return code==expected?0:1;
}
static int fail(char **error,const char *stage,const char *reply){
  if(error)*error=fmt("SMTP %s failed: %s",stage,reply?reply:"connection closed or malformed reply","","");
  return -1;
}
int smtp_run_session(smtp_transport t,const char *helo,const char *from,const char *to,const char *subject,const char *body,char **error){
  if(error)*error=NULL;
  if(!helo||!from||!to||!subject||smtp_has_newline(helo)||smtp_has_newline(from)||smtp_has_newline(to)||smtp_has_newline(subject))return fail(error,"input","invalid CR or LF in field");
  smtp_reader r;if(smtp_reader_init(&r,t))return fail(error,"transport","invalid callbacks");char *reply=NULL;int code;
  if(smtp_read_reply(&r,&code,&reply)||code!=220){int x=fail(error,"greeting",reply);free(reply);return x;}free(reply);reply=NULL;
  size_t fn=strlen(from)+8,tn=strlen(to)+6;char *fa=malloc(fn),*ta=malloc(tn);
  if(!fa||!ta){free(fa);free(ta);return fail(error,"input","out of memory");}
  (void)snprintf(fa,fn,"FROM:<%s>",from);(void)snprintf(ta,tn,"TO:<%s>",to);
  const char *verbs[]={"HELO","MAIL","RCPT","DATA"},*args[]={helo,fa,ta,NULL};int expected[]={250,250,250,354};
  for(size_t i=0;i<4;i++){char *cmd=smtp_command(verbs[i],args[i]);if(!cmd){free(fa);free(ta);return fail(error,verbs[i],"out of memory");}
    int s=smtp_send_command(&r,cmd,expected[i],&reply);free(cmd);if(s){int x=fail(error,verbs[i],reply);free(reply);free(fa);free(ta);return x;}free(reply);reply=NULL;}
  free(fa);free(ta);char *payload=smtp_data_payload(from,to,subject,body);if(!payload)return fail(error,"DATA","could not build message");
  int s=smtp_send_command(&r,payload,250,&reply);free(payload);if(s){int x=fail(error,"message",reply);free(reply);return x;}free(reply);reply=NULL;
  char *quit=smtp_command("QUIT",NULL);if(!quit)return fail(error,"QUIT","out of memory");s=smtp_send_command(&r,quit,221,&reply);free(quit);
  if(s){int x=fail(error,"QUIT",reply);free(reply);return x;}free(reply);return 0;
}
int smtp_socket_connect(const char *server,const char *port,char **error){
  if(error)*error=NULL;
  struct addrinfo hints,*list=NULL;memset(&hints,0,sizeof hints);hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;
  int e=getaddrinfo(server,port,&hints,&list);if(e){if(error)*error=dupstr(gai_strerror(e));return -1;}int fd=-1,saved=ECONNREFUSED;
  for(struct addrinfo *a=list;a;a=a->ai_next){fd=socket(a->ai_family,a->ai_socktype,a->ai_protocol);if(fd<0){saved=errno;continue;}if(connect(fd,a->ai_addr,a->ai_addrlen)==0)break;saved=errno;close(fd);fd=-1;}
  freeaddrinfo(list);if(fd<0&&error)*error=dupstr(strerror(saved));return fd;
}
ssize_t smtp_socket_read(void *ctx,void *buf,size_t len){int fd=*(int*)ctx;ssize_t n;do{n=recv(fd,buf,len,0);}while(n<0&&errno==EINTR);return n;}
ssize_t smtp_socket_write(void *ctx,const void *buf,size_t len){int fd=*(int*)ctx;ssize_t n;do{
#ifdef MSG_NOSIGNAL
  n=send(fd,buf,len,MSG_NOSIGNAL);
#else
  n=send(fd,buf,len,0);
#endif
  }while(n<0&&errno==EINTR);return n;}
