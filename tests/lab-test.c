#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "harness/unity.h"
#include "../src/lab.h"

typedef struct { const char *input; size_t at, chunk; char output[8192]; size_t out; } script;
static ssize_t fake_read(void *ctx,void *buf,size_t len){
 script *s=ctx;if(!s->input[s->at])return 0;size_t n=strlen(s->input+s->at);if(n>len)n=len;if(s->chunk&&n>s->chunk)n=s->chunk;
 memcpy(buf,s->input+s->at,n);s->at+=n;return (ssize_t)n;
}
static ssize_t fake_write(void *ctx,const void *buf,size_t len){
 script *s=ctx;if(len>3)len=3;if(s->out+len>=sizeof s->output)return -1;memcpy(s->output+s->out,buf,len);s->out+=len;s->output[s->out]='\0';return (ssize_t)len;
}
static ssize_t failed_read(void *ctx,void *buf,size_t len){(void)ctx;(void)buf;(void)len;return -1;}
static ssize_t failed_write(void *ctx,const void *buf,size_t len){(void)ctx;(void)buf;(void)len;return -1;}
static ssize_t oversized_read(void *ctx,void *buf,size_t len){(void)ctx;(void)buf;return (ssize_t)(len+1);}
void setUp(void){} void tearDown(void){}
static void test_reply_helpers(void){
 TEST_ASSERT_EQUAL_INT(250,smtp_reply_code("250-ok\r\n"));TEST_ASSERT_EQUAL_INT(-1,smtp_reply_code("xx"));
 TEST_ASSERT_FALSE(smtp_reply_is_final("250-more\r\n"));TEST_ASSERT_TRUE(smtp_reply_is_final("250 done\r\n"));
}
static void test_helper_edge_cases(void){
 TEST_ASSERT_FALSE(smtp_has_newline(NULL));TEST_ASSERT_TRUE(smtp_has_newline("a\rb"));TEST_ASSERT_TRUE(smtp_has_newline("a\nb"));
 TEST_ASSERT_EQUAL_INT(-1,smtp_reply_code(NULL));TEST_ASSERT_EQUAL_INT(-1,smtp_reply_code("1x0 bad"));
 TEST_ASSERT_EQUAL_INT(-1,smtp_reply_code("250!bad"));TEST_ASSERT_EQUAL_INT(250,smtp_reply_code("250"));
 char *s=smtp_command("QUIT",NULL);TEST_ASSERT_EQUAL_STRING("QUIT\r\n",s);free(s);
 TEST_ASSERT_NULL(smtp_command(NULL,NULL));TEST_ASSERT_NULL(smtp_command("BAD\nCOMMAND",NULL));
 TEST_ASSERT_NULL(smtp_command("HELO","bad\rhost"));
 s=smtp_dot_stuff(NULL);TEST_ASSERT_EQUAL_STRING("",s);free(s);
 s=smtp_dot_stuff("one\rtwo\r\n.three\n");TEST_ASSERT_EQUAL_STRING("one\r\ntwo\r\n..three\r\n",s);free(s);
 s=smtp_data_payload("a","b","","body\n");TEST_ASSERT_EQUAL_STRING("From: a\r\nTo: b\r\nSubject: \r\n\r\nbody\r\n.\r\n",s);free(s);
 TEST_ASSERT_NULL(smtp_data_payload(NULL,"b","s",""));TEST_ASSERT_NULL(smtp_data_payload("a",NULL,"s",""));
 TEST_ASSERT_NULL(smtp_data_payload("a","b",NULL,""));TEST_ASSERT_NULL(smtp_data_payload("a","b\n","s",""));
}
static void test_commands_and_payload(void){
 char *s=smtp_command("HELO","localhost");TEST_ASSERT_EQUAL_STRING("HELO localhost\r\n",s);free(s);
 s=smtp_dot_stuff(".one\n..two\r\nthree");TEST_ASSERT_EQUAL_STRING("..one\r\n...two\r\nthree",s);free(s);
 s=smtp_data_payload("a@b","c@d","hi",".first\nlast");
 TEST_ASSERT_EQUAL_STRING("From: a@b\r\nTo: c@d\r\nSubject: hi\r\n\r\n..first\r\nlast\r\n.\r\n",s);free(s);
 TEST_ASSERT_NULL(smtp_data_payload("a\nb","c","s","body"));
}
static void test_fragmented_multiline_reply(void){
 script s={"250-one\r\n250 two\r\n",0,1,"",0};smtp_transport t={fake_read,fake_write,&s};smtp_reader r;TEST_ASSERT_EQUAL_INT(0,smtp_reader_init(&r,t));
 int code=0;char *reply=NULL;TEST_ASSERT_EQUAL_INT(0,smtp_read_reply(&r,&code,&reply));TEST_ASSERT_EQUAL_INT(250,code);
 TEST_ASSERT_EQUAL_STRING("250-one\r\n250 two\r\n",reply);free(reply);
}
static void test_complete_session(void){
 script s={"220 ready\r\n250 hello\r\n250 sender\r\n250 recipient\r\n354 go\r\n250 queued\r\n221 bye\r\n",0,2,"",0};
 smtp_transport t={fake_read,fake_write,&s};char *error=NULL;
 TEST_ASSERT_EQUAL_INT(0,smtp_run_session(t,"host","a@b","c@d","subject",".body",&error));TEST_ASSERT_NULL(error);
 TEST_ASSERT_EQUAL_STRING("HELO host\r\nMAIL FROM:<a@b>\r\nRCPT TO:<c@d>\r\nDATA\r\nFrom: a@b\r\nTo: c@d\r\nSubject: subject\r\n\r\n..body\r\n.\r\nQUIT\r\n",s.output);
}
static void test_session_error_reports_reply(void){
 script s={"220 ready\r\n550 no helo\r\n",0,0,"",0};smtp_transport t={fake_read,fake_write,&s};char *error=NULL;
 TEST_ASSERT_EQUAL_INT(-1,smtp_run_session(t,"host","a@b","c@d","","body",&error));TEST_ASSERT_NOT_NULL(strstr(error,"550 no helo"));free(error);
}
static void test_transport_error_paths(void){
 script s={"",0,0,"",0};smtp_transport good={fake_read,fake_write,&s},no_read={NULL,fake_write,&s},no_write={fake_read,NULL,&s};smtp_reader r;char *line=NULL,*reply=NULL;int code;
 TEST_ASSERT_EQUAL_INT(-1,smtp_reader_init(NULL,good));TEST_ASSERT_EQUAL_INT(-1,smtp_reader_init(&r,no_read));TEST_ASSERT_EQUAL_INT(-1,smtp_reader_init(&r,no_write));
 TEST_ASSERT_EQUAL_INT(0,smtp_reader_init(&r,good));TEST_ASSERT_EQUAL_INT(-1,smtp_read_line(NULL,&line));TEST_ASSERT_EQUAL_INT(-1,smtp_read_line(&r,NULL));
 TEST_ASSERT_EQUAL_INT(-1,smtp_read_line(&r,&line));TEST_ASSERT_NULL(line);
 TEST_ASSERT_EQUAL_INT(-1,smtp_read_reply(&r,NULL,&reply));TEST_ASSERT_EQUAL_INT(-1,smtp_read_reply(&r,&code,NULL));
 TEST_ASSERT_EQUAL_INT(-1,smtp_write_all(NULL,"x",1));TEST_ASSERT_EQUAL_INT(-1,smtp_write_all(&no_write,"x",1));TEST_ASSERT_EQUAL_INT(-1,smtp_write_all(&good,NULL,1));
 smtp_transport badw={fake_read,failed_write,&s};TEST_ASSERT_EQUAL_INT(-1,smtp_write_all(&badw,"x",1));
 smtp_transport badr={oversized_read,fake_write,&s};TEST_ASSERT_EQUAL_INT(0,smtp_reader_init(&r,badr));TEST_ASSERT_EQUAL_INT(-1,smtp_read_line(&r,&line));
 smtp_transport readfail={failed_read,fake_write,&s};TEST_ASSERT_EQUAL_INT(0,smtp_reader_init(&r,readfail));TEST_ASSERT_EQUAL_INT(-1,smtp_send_command(&r,"NOOP\r\n",250,&reply));
 TEST_ASSERT_EQUAL_INT(0,smtp_reader_init(&r,badw));TEST_ASSERT_EQUAL_INT(-1,smtp_send_command(&r,"NOOP\r\n",250,&reply));
}
static void test_bad_replies(void){
 script malformed={"bad reply\r\n",0,0,"",0};smtp_transport t={fake_read,fake_write,&malformed};smtp_reader r;char *reply=NULL;int code;
 TEST_ASSERT_EQUAL_INT(0,smtp_reader_init(&r,t));TEST_ASSERT_EQUAL_INT(-1,smtp_read_reply(&r,&code,&reply));
 script mismatch={"250-more\r\n550 done\r\n",0,0,"",0};t.context=&mismatch;TEST_ASSERT_EQUAL_INT(0,smtp_reader_init(&r,t));TEST_ASSERT_EQUAL_INT(-1,smtp_read_reply(&r,&code,&reply));
 script wrong={"550 nope\r\n",0,0,"",0};t.context=&wrong;TEST_ASSERT_EQUAL_INT(0,smtp_reader_init(&r,t));TEST_ASSERT_EQUAL_INT(1,smtp_send_command(&r,"NOOP\r\n",250,&reply));free(reply);
}
static void assert_session_fails(const char *replies,const char *text){
 script s={replies,0,0,"",0};smtp_transport t={fake_read,fake_write,&s};char *error=NULL;
 TEST_ASSERT_EQUAL_INT(-1,smtp_run_session(t,"host","a@b","c@d","s","body",&error));TEST_ASSERT_NOT_NULL(strstr(error,text));free(error);
}
static void test_all_session_failures(void){
 script s={"",0,0,"",0};smtp_transport t={fake_read,fake_write,&s};char *error=NULL;
 TEST_ASSERT_EQUAL_INT(-1,smtp_run_session(t,"bad\n","a","b","s","",&error));free(error);error=NULL;
 smtp_transport invalid={NULL,fake_write,&s};TEST_ASSERT_EQUAL_INT(-1,smtp_run_session(invalid,"h","a","b","s","",&error));free(error);
 assert_session_fails("550 greeting\r\n","550 greeting");
 assert_session_fails("220 ok\r\n550 helo\r\n","550 helo");
 assert_session_fails("220 ok\r\n250 ok\r\n550 mail\r\n","550 mail");
 assert_session_fails("220 ok\r\n250 ok\r\n250 ok\r\n550 rcpt\r\n","550 rcpt");
 assert_session_fails("220 ok\r\n250 ok\r\n250 ok\r\n250 ok\r\n550 data\r\n","550 data");
 assert_session_fails("220 ok\r\n250 ok\r\n250 ok\r\n250 ok\r\n354 go\r\n550 body\r\n","550 body");
 assert_session_fails("220 ok\r\n250 ok\r\n250 ok\r\n250 ok\r\n354 go\r\n250 queued\r\n550 quit\r\n","550 quit");
}
static void test_socket_callbacks(void){
 int sockets[2];TEST_ASSERT_EQUAL_INT(0,socketpair(AF_UNIX,SOCK_STREAM,0,sockets));char buf[4]={0};
 TEST_ASSERT_EQUAL_INT(3,(int)smtp_socket_write(&sockets[0],"abc",3));TEST_ASSERT_EQUAL_INT(3,(int)smtp_socket_read(&sockets[1],buf,3));TEST_ASSERT_EQUAL_STRING("abc",buf);
 close(sockets[0]);close(sockets[1]);char *error=NULL;TEST_ASSERT_EQUAL_INT(-1,smtp_socket_connect("invalid.invalid","25",&error));TEST_ASSERT_NOT_NULL(error);free(error);
 error=NULL;TEST_ASSERT_EQUAL_INT(-1,smtp_socket_connect("127.0.0.1","0",&error));TEST_ASSERT_NOT_NULL(error);free(error);
}
int main(void){UNITY_BEGIN();RUN_TEST(test_reply_helpers);RUN_TEST(test_commands_and_payload);
 RUN_TEST(test_helper_edge_cases);RUN_TEST(test_fragmented_multiline_reply);RUN_TEST(test_complete_session);
 RUN_TEST(test_session_error_reports_reply);RUN_TEST(test_transport_error_paths);RUN_TEST(test_bad_replies);
 RUN_TEST(test_all_session_failures);RUN_TEST(test_socket_callbacks);return UNITY_END();}
