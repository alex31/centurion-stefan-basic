#include "bytecode.h"
uint16_t bas_arg;
char bas_name[BAS_PATTERN_MAX + 1],bas_current[11];
static char upper(char c) {return c>='a'&&c<='z'?c-'a'+'A':c;}
static const char *spaces(const char *p){while(*p==' '||*p=='\t')p++;return p;}
static unsigned equal(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
unsigned bas_parse(const char *p)
{
 char command[7];unsigned n=0,op;uint16_t value=0;
 p=spaces(p);if(*p++!='.')return BC_NONE;
 while(*p>='A'&&*p<='Z'||*p>='a'&&*p<='z'){
  if(n==6)goto syntax;command[n++]=upper(*p++);
 }command[n]=0;p=spaces(p);bas_arg=0;bas_name[0]=0;
 if(equal(command,"RUN"))op=BC_RUN;
 else if(equal(command,"LIST"))op=BC_LIST;
 else if(equal(command,"NEW"))op=BC_NEW;
 else if(equal(command,"EDIT"))op=BC_EDIT;
 else if(equal(command,"RENUM"))op=BC_RENUM;
 else if(equal(command,"SAVE"))op=BC_SAVE;
 else if(equal(command,"LOAD"))op=BC_LOAD;
 else if(equal(command,"DIR"))op=BC_DIR;
 else if(equal(command,"BYE"))op=BC_BYE;
 else if(equal(command,"HELP"))op=BC_HELP;
 else goto syntax;
 if(op==BC_RUN||op==BC_LIST||op==BC_EDIT||op==BC_RENUM){
  n=0;while(*p>='0'&&*p<='9'){
   if(value>6553U||(value==6553U&&*p>'5'))goto syntax;
   value=value*10U+(*p++-'0');n++;
  }
  if((n&&!value)||(op==BC_RENUM&&!n))goto syntax;
  bas_arg=value;
  if(op==BC_EDIT){
   p=spaces(p);
   if(upper(p[0])=='E'&&upper(p[1])=='A'&&upper(p[2])=='S'&&upper(p[3])=='Y'){
    p+=4;
   }
  }
 } else if(op==BC_SAVE||op==BC_LOAD||op==BC_DIR){
  if(*p){
   unsigned literals=0;
   if(*p++!='"')goto syntax;
   n=0;
   while(*p&&*p!='"'){
    char c=upper(*p++);
    if(c=='*'&&op==BC_DIR){
     /* Collapse adjacent stars so ten literals need at most eleven stars. */
     if(n&&bas_name[n-1]=='*')continue;
    }else{
     if(!((c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||(op==BC_DIR&&c=='@')))goto syntax;
     if(literals==BAS_NAME_MAX)goto syntax;
     ++literals;
    }
    bas_name[n++]=c;
   }
   if(*p++!='"'||!n)goto syntax;bas_name[n]=0;
  }
  if(op==BC_LOAD&&!bas_name[0])goto syntax;
  if(op==BC_DIR&&!bas_name[0]){bas_name[0]='*';bas_name[1]=0;}
 }
 if(*spaces(p))goto syntax;
 if(op==BC_HELP){
  puts(".RUN .LIST [line] .NEW .EDIT [line] .RENUM interval");
  puts(".SAVE [\"name\"] .LOAD \"name\" .DIR \"*text*\" .BYE");
 }
 return op;
syntax: puts("Command syntax error; .HELP");return BC_ERROR;
}

/* Permanent dispatcher: every cold operation returns before the next group
   replaces its code. RUN compiles the stored program before entering the VM. */
int bas_command(char *line)
{
 unsigned op;
 while(*line==' '||*line=='\t')line++;
 if(*line!='.')return 0;
 op=bas_parse(line);
 switch(op){
 case BC_NONE:return 0;
 case BC_RUN:bas_run_program(bas_arg);break;
 case BC_LIST:listlines(bas_arg,bas_arg?bas_arg:65535U);if(bas_arg)outcr();break;
 case BC_NEW:xnew();ib_reset();bas_current[0]=0;break;
 case BC_RENUM:if(bas_renum(bas_arg)){resetbasicstate();ib_reset();puts("Renumbered");}break;
 case BC_EDIT:bas_editor(bas_arg);break;
 case BC_SAVE:case BC_LOAD:case BC_DIR:if(bas_query(op))bas_files(op);if(bas_pending)bas_publish();if(bas_loaded){resetbasicstate();ib_reset();}break;
 case BC_BYE:stb_done=1;break;
 case BC_HELP:break;
 }
 /* Do not leave a dot command in the BASIC parser's input stream. */
 ibuffer[0]=ibuffer[1]=0;bi=ibuffer;st=SINT;token=EOL;
 if(er)reseterror();return 1;
}
