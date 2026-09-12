#include <stdlib.h>
#include <string.h>
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
void setUp(void){} void tearDown(void){}
static void test_reply_helpers(void){
 TEST_ASSERT_EQUAL_INT(250,smtp_reply_code("250-ok\r\n"));TEST_ASSERT_EQUAL_INT(-1,smtp_reply_code("xx"));
 TEST_ASSERT_FALSE(smtp_reply_is_final("250-more\r\n"));TEST_ASSERT_TRUE(smtp_reply_is_final("250 done\r\n"));
}
static void test_commands_and_payload(void){
 char *s=smtp_command("HELO","localhost");TEST_ASSERT_EQUAL_STRING("HELO localhost\r\n",s);free(s);
 s=smtp_dot_stuff(".one\n..two\r\nthree");TEST_ASSERT_EQUAL_STRING("..one\r\n...two\r\nthree",s);free(s);
 s=smtp_data_payload("a@b","c@d","hi",".first\nlast");
 TEST_ASSERT_EQUAL_STRING("From: a@b\r\nTo: c@d\r\nSubject: hi\r\n\r\n..first\r\nlast\r\n.\r\n",s);free(s);
 TEST_ASSERT_NULL(smtp_data_payload("a\nb","c","s","body"));
}
static void test_fragmented_multiline_reply(void){
 script s={"250-one\r\n250 two\r\n",0,1,"",0};smtp_transport t={fake_read,fake_write,&s};smtp_reader r;smtp_reader_init(&r,t);
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
int main(void){UNITY_BEGIN();RUN_TEST(test_reply_helpers);RUN_TEST(test_commands_and_payload);
 RUN_TEST(test_fragmented_multiline_reply);RUN_TEST(test_complete_session);RUN_TEST(test_session_error_reports_reply);return UNITY_END();}
