#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<stdbool.h> 
#include<termios.h> 
#include<unistd.h>  
#include<errno.h>

#define MAX_BLOCKS_NUM 256       //最大数据块数量是256，则数据块总大小是256KB
#define MAX_BLOCK_SIZE 1025       //数据块数据容量,一个数据块的大小是1KB,最有1位用来存储'\0',表示字符串结尾！
#define MAX_DATANODE_NUM 2       //每个文件最多占用的数据块的数量
#define MAX_INODE_NUM  512       //i节点的最大数目，即系统允许容纳文件的最大数量
#define ECHOFLAGS (ECHO | ECHOE | ECHOK | ECHONL) 

/*********************结构定义**********************/
/*---------用户---------*/
typedef struct user
{
    char account[10];                    
    char password[10];                 
} user;
/*-------磁盘数据块--------*/
typedef struct datanode          //用于记录文件数据保存在哪个数据块的哪个范围
{
    int pos;                  //数据块号
    int begin;                //数据开始位置
    int end;                  //数据结束位置
} datanode;
/*------索引节点（文件信息)-----*/
typedef struct inode
{
    char filename[30];	 //文件名
    int num;             //索引节点数目，即文件数目
    char code[5];       //文件保护码
    int size;		 //文件大小
    datanode fat[MAX_DATANODE_NUM];   //文件使用数据块的情况（最多两个数据块）
    int node_num;        //占用的数据块个数
} inode;
typedef struct dirEntry         //i节点链表
{
    inode ind;
    struct dirEntry *next;
} dirEntry;
/*--------内存数据块---------*/
typedef struct block
{
    char content[MAX_BLOCK_SIZE];           //数据块内容最大长度为 1024
    int pos;                                //在空闲表中的位置
    bool isRevised;                         //用于记录数据块是否进行数据修改
    int offset;                             //记录当前数据的长度
} block;

/***********************全局变量***********************/
char blockspath[30] = "userdata/blocksdisk.disk";  //  所有数据(即虚拟磁盘)地址
char userspath[30] = "userdata/users.us";    //存储用户名 密码
static user currUser;             //当前用户
static dirEntry *DIR;   //链表头指针
static int curr_inode_num = 0;          //当前i节点数量，即文件数量
int max_node = 0;            //最大的inode编号
static int FBT[MAX_BLOCKS_NUM];   //空闲块信息
static char *dirpath;
static char fbtpath[30] = "userdata/FBT.fbt";
static dirEntry* selectEntry;      //当selectEntry==NULL时，证明没有打开文件
static dirEntry* currEntry;

/***********************函数声明***********************/
void getUser();
int login();
int regist();
void createBlocksDisk();
FILE* createDir();
void initDir(char *);
void initFBT();
int getFreeBlock();
void saveDir();
void saveBlock(block bk);
void saveFBT();
int op_create(char *filename);
dirEntry* op_del(char *filename);
int LgRg();
void help();
void dir();
void create();
void del();
void open();
void nclose();
void nread();
void nwrite();
void op_cover();
void op_append();

/**********************用户管理***********************/
/*----控制是否开启输入回显功能-----*/  
//如果option为0，则关闭回显，为1则打开回显  
int set_disp_mode(int fd,int option)  
{  
   int err;  
   struct termios term;  
   if(tcgetattr(fd,&term)==-1){  
     perror("Cannot get the attribution of the terminal");  
     return 1;  
   }  
   if(option)  
        term.c_lflag|=ECHOFLAGS;  
   else  
        term.c_lflag &=~ECHOFLAGS;  
   err=tcsetattr(fd,TCSAFLUSH,&term);  
   if(err==-1 && err==EINTR){  
        perror("Cannot set the attribution of the terminal");  
        return 1;  
   }  
   return 0;  
} 
/*---输入账号，密码---*/
void getUser()
{
    printf("account(length<10): ");
    scanf("%s",currUser.account);
    //getchar();//将回车符屏蔽掉
    //首先关闭输出回显，这样输入密码时就不会显示输入的字符信息  
    set_disp_mode(STDIN_FILENO,0); 
    printf("password(length<10): ");
    scanf("%s", currUser.password);
    //getpasswd(currUser.password, sizeof(currUser.password));       
    set_disp_mode(STDIN_FILENO,1); //开启回显
}
/*-------登录函数------*/
int login()
{
    getUser();
    FILE *ffp = fopen(userspath, "r");
    if(ffp==NULL)
    {
        printf("FILE ERROR !");
        exit(0);
    }
    else
    {
        user temp;
        while(!feof(ffp))
        {
            fscanf(ffp,"%s",temp.account);
            fscanf(ffp,"%s",temp.password);
            if((!strcmp(temp.account, currUser.account))&&(!strcmp(temp.password, currUser.password)))
            {
                fclose(ffp);
                return 0;
            }
        }
        fclose(ffp);
        return -1;
    }
}
/*------注册函数------*/
int regist()
{
    getUser();
    FILE *fp = fopen(userspath, "a");
    if(fp==NULL)
    {
        printf("FILE ERROR !");
        return -1;
    }
    else
    { 
        fprintf(fp,"%s ",currUser.account);
        fprintf(fp," %s\n",currUser.password);
        fclose(fp);
        return 0;
    }
}
/*----登录注册操作----*/
int LgRg()
{
    while(1)
    {
        char com[10];
        printf("请选择[login]或[register]: ");
        scanf("%s",com);
        if(strcmp(com,"login")==0)
        {
            if(login())
            {
                printf("\n登录失败 ！\n");
                return -1;
            }
            else
            {
                printf("\n登录成功 ！\n");
                return 0;
            }
        }
        if(strcmp(com,"register")==0)
        {
            if(regist())
            {
                printf("\n注册失败 ！\n");
                return -1;
            }
            else
            {
                printf("\n注册成功 ！\n");
                return 0;
            }
        }
    }
}

/*********************目录管理***********************/
/*-------创建该用户的目录文件-----*/
FILE* createDir(char *dirpath)
{
    FILE *fp = fopen(dirpath, "w");
    if(fp==NULL)
    {
        printf("创建用户目录失败 !\n");
        while(1);
    }
    else
    {
        fclose(fp);
        FILE *f = fopen(dirpath, "r");
        return f;
    }
}
/*---根据用户名生成用户目录文件路径---*/
char *getDirpath(char* username)
{
    char *res;
    res = (char*)malloc(50*sizeof(char));
    char t[10] = "userdata/";
    char tail[5] = ".dir";
    strcpy(res, t);
    strcat(res, username);
    strcat(res, tail);
    return res;
}
/*-----初始化用户目录------*/
//目录文件读入内存
void initDir(char *dirpath)
{
    FILE *p ;
    p = fopen(dirpath, "r");
    if(p==NULL)
    {
        p = createDir(dirpath);
    }
    dirEntry* pp = (dirEntry*)malloc(sizeof(dirEntry));
    pp->next=NULL;
    DIR = NULL;
    int nm = 0;
    while(!feof(p))
    {
        if(nm!=0)
        {
            dirEntry* pt = (dirEntry*)malloc(sizeof(dirEntry));   //新目录节点插入到目录链表
            pt->next=NULL;
            pp->next = pt;
            pp=pt;
        }
        int r = fscanf(p,"%s",pp->ind.filename);
        if(r==EOF)
        {
            break;
        }
        fscanf(p,"%d",&(pp->ind.num));
        if(max_node < pp->ind.num)
        {
            max_node = pp->ind.num;
        }
        fscanf(p,"%s",pp->ind.code);
        fscanf(p,"%d",&pp->ind.size);
        if(pp->ind.size!=0)
        {
            fscanf(p,"%d",&pp->ind.node_num);
            int j;
            int temp = 0;
            for(j=0; j<pp->ind.node_num; j++)
            {
                fscanf(p,"%d",&pp->ind.fat[j].pos);
                fscanf(p,"%d",&pp->ind.fat[j].begin);
                fscanf(p,"%d",&pp->ind.fat[j].end);
            }
        }
        curr_inode_num++;
        if(curr_inode_num == MAX_INODE_NUM)
        {
            break;
        }
        if(nm==0)
        {
            DIR=pp;
            nm = 1;
        }
        currEntry = pp;
    }
    fclose(p);
}
/*-----保存索引节点内容到目录文件-----*/
void saveDir()
{
    FILE *fp = fopen(getDirpath(currUser.account), "w");
    if(fp==NULL)
    {
        printf("修改索引节点出现错误!\n");
        while(1);
    }
    else
    {
        int j;
        dirEntry *p = DIR;
        while(p!=NULL)
        {
            fprintf(fp," %s",p->ind.filename);
            fprintf(fp," %d",p->ind.num);
            fprintf(fp," %s",p->ind.code);
            fprintf(fp," %d",p->ind.size);
            if(p->ind.size!=0)
            {
                fprintf(fp," %d",p->ind.node_num);
                for(j=0; j<p->ind.node_num; j++)
                {
                    fprintf(fp," %d",p->ind.fat[j].pos);
                    fprintf(fp," %d",p->ind.fat[j].begin);
                    fprintf(fp," %d",p->ind.fat[j].end);
                }
            }
            p=p->next;
        }
        fclose(fp);
    }
}
/*-----查找该文件名称对应的i节点-----*/
dirEntry* isInDir(char *filename)
{
    int i;
    dirEntry *pt = DIR;
    while(pt!=NULL)
    {
        if(strcmp(pt->ind.filename, filename)==0)
        {
            return pt;
        }
        pt = pt->next;
    }
    return NULL;
}

/***********************磁盘块管理***********************/
/*---初始化磁盘块，创建必要的系统文件----*/
void createBlocksDisk()
{
    //初始化磁盘块
    FILE* fp = fopen(blockspath, "w");
    if(fp==NULL)
    {
        printf("磁盘初始化错误\n");
    }
    else
    {
        int i, j;
        for(i=0; i<MAX_BLOCKS_NUM; i++)
        {
            for(j=0; j<MAX_BLOCK_SIZE; j++)
            {
                fputc('$',fp);
            }
        }
        fclose(fp);
    }
    //初始化空闲块列表文件
    FILE *p = fopen(fbtpath, "w");
    if(p==NULL)
    {
        printf("FBT创建失败!\n");
    }
    else
    {
        int i;
        for(i = 0; i<MAX_BLOCKS_NUM; i++)
        {
            FBT[i] = 0;
            fprintf(p," %d",0);
        }
        fclose(p);
    }
}
/*-----将数据块内容写入磁盘-----*/
void saveBlock(block bk)
{
    FILE *fp = fopen(blockspath, "r+");
    long offset = bk.pos * (MAX_BLOCK_SIZE-1);
    fseek(fp,offset,SEEK_SET);
    fwrite(bk.content,sizeof(char),strlen(bk.content),fp);
    fclose(fp);
    FBT[bk.pos] = 1;
}

/**********************空闲块管理********************/
/*-----初始化空闲块记录文件-----*/
//空闲块信息读入内存
void initFBT()
{
    FILE* fp;
    fp = fopen(fbtpath, "r");
    int i=0;
    while(!feof(fp))
    {
        fscanf(fp,"%d",&FBT[i++]);
        if(i==MAX_BLOCK_SIZE-1)
        {
            break;
        }
    }
}
/*------保存空闲块记录文件-------*/
void saveFBT()
{
    FILE* fp = fopen(fbtpath, "w");
    int i;
    for(i =0; i<MAX_BLOCKS_NUM; i++)
    {
        fprintf(fp," %d",FBT[i]);
    }
    fclose(fp);
}
/*---根据位示图获取空闲数据块，返回申请到的块号---*/
int getFreeBlock()    //因为文件最多占用两个数据块，因此在未用数据块的时候要保证至少有两个数据块
{
    int k;
    for(k=0; k<MAX_BLOCKS_NUM; k++)
    {
        if(FBT[k]==0)  //空闲数据块
        {
		return k;
        }
    }
    return -1;
}

/***********************功能实现********************/
/*-------显示目录中的所有文件------*/
void dir()
{
    printf("文件名\t物理块号\t保护码\t\t文件长度\n");
    int i=0;
    dirEntry *pt = DIR;
    while(pt!=NULL)
    {
        if(pt->ind.node_num==1)
	{
             printf("%s\t%d\t\t%s\t\t%d\n",pt->ind.filename,pt->ind.fat[0].pos,pt->ind.code,pt->ind.size);
	}
	else if(pt->ind.node_num==0)
	{
             printf("%s\t%s\t\t%s\t\t%d\n",pt->ind.filename,"null",pt->ind.code,pt->ind.size);
	}
	else
	{
	     printf("%s\t%d %d\t\t%s\t\t%d\n",pt->ind.filename,pt->ind.fat[0].pos,pt->ind.fat[1].pos,pt->ind.code,pt->ind.size);
	}
        pt=pt->next;
    }
}
/*------------创建文件-----------*/
void create()
{
    //获取空闲数据块
    int bkNum = getFreeBlock();
    if(bkNum==-1)
    {
        printf("数据块已用完,内存不足!\n");
        return;
    }
    char tmp;
    //创建新的索引节点
    dirEntry *pt = (dirEntry*)malloc(sizeof(dirEntry));
    pt->next=NULL;
    while(1)
    {
        printf("文件名: ");
        scanf("%s",pt->ind.filename);
        if(isInDir(pt->ind.filename)!=NULL)
        {
            printf("文件名已存在 !\n请重新输入:\n");
        }
        else
        {
            break;
        }
    }
    pt->ind.num = curr_inode_num++;
    char code[5] = "rwx";   //文件保护码默认为"rwx";
    strcpy(pt->ind.code, code);
    pt->ind.size = 0;
    pt->ind.node_num = 0;
    if(currEntry==NULL)
    {
        DIR = pt;
    }
    //将该文件加到当前正在处理文件的链表上
    else
    {
        currEntry->next = pt;
    }
    currEntry = pt;
    saveDir();
    saveFBT();
    printf("[%s] 创建成功!\n",pt->ind.filename);
}
/*-------显示帮助------*/
void help()
{
	printf("欢迎您使用虚拟文件管理系统\n");
	printf("以下是本系统支持的指令:\n");
	printf("exit : 退出\n");
	printf("help : 帮助\n");
	printf("dir : 查看目录中的所有文件\n");
	printf("create : 新建文件\n");
	printf("delete : 删除文件\n");
	printf("open : 打开文件\n");
	printf("read : 读文件（必须先打开文件）\n");
	printf("write : 写文件（必须先打开文件）\n");
	printf("close : 关闭文件\n");
}
/*-------删除文件-------*/
void del()
{
    char tmp[30];
    printf("请输入要删除的文件名: ");
    scanf("%s",tmp);
    if(isInDir(tmp)==NULL)
    {
        printf("不存在这个文件\n");
        return;
    }
    else
    {
        dirEntry *dle = op_del(tmp);  //删除并返回该索引节点
        if(dle!=NULL)
        {
            int i;
            for(i=0; i<dle->ind.node_num; i++)
            {
                FBT[dle->ind.fat[i].pos]=0;  //该块置为空闲
            }
        }
    }
    saveDir();
    saveFBT();
}
//即删除链表上的该索引节点
dirEntry* op_del(char* filename)
{
    dirEntry* res = DIR;
    if(res==NULL)
    {
        printf("文件不存在!\n");
        return res;
    }
    if(res->next==NULL)
    {
        if(strcmp(res->ind.filename,filename)==0)
        {
            DIR = NULL;
            currEntry=NULL;
            printf("删除成功!\n");
            return res;
        }
        else
        {
            return NULL;
        }
    }
    if(strcmp(res->ind.filename,filename)==0)
    {
        DIR = res->next;
        printf("删除成功!\n");
        return res;
    }
    while(res->next!=NULL)
    {
        if(strcmp(res->next->ind.filename,filename)==0)
        {
            dirEntry* r = res->next;
            res->next = r->next;
            printf("删除成功!\n");
            return r;
        }
        res = res->next;
    }
    printf("删除失败!\n");
    return NULL;
}
/*--------打开文件-------*/
void open()
{
    char file[50];
    printf("请输入文件名: ");
    scanf("%s",file);
    selectEntry = isInDir(file);  //当前打开的文件，返回文件索引节点的指针
    if(selectEntry==NULL)
    {
        printf("没有这个文件 !\n");
    }
    else
    {
        printf("文件[%s]已打开,输入close关闭.\n",file);
        int c = 0;
        while(1)
        {
            if(c==1)
            {
                break;
            }
            char cmd[10];
            printf(">>> ");
            scanf("%s",cmd);
            if(strcmp(cmd, "read")==0)
            {
                nread();
            }
            else if(strcmp(cmd, "write")==0)
            {
                nwrite();
            }
            else if(strcmp(cmd, "close")==0)
            {
                nclose();
                break;
            }
            else
            {
                printf("请先关闭文件，再执行其他操作！\n");
            }
        }

    }

}
/*-------写入文件------*/
void nwrite()
{
    char sel[10];
    while(1)
    {
        printf("[append]:在原文件基础上新增数据.\n[cover]:覆盖原数据\n[cancle]:取消\n");
        printf(">>> ");
        scanf("%s",sel);
        if(strcmp(sel,"cancle")==0)
        {
            printf("取消 !\n");
            break;
        }
        if(strcmp(sel,"cover")==0)
        {
            op_cover();
            break;
        }
        if(strcmp(sel,"append")==0)
        {
            if(selectEntry->ind.size==0)
            {
                op_cover();
            }
            else
            {
                op_append();
            }
            break;
        }
        printf("无效的指令\n");
    }
}
/*覆盖原数据*/
void op_cover()
{
    //释放原数据块，更新FBT
    int f;
    for(f=0; f<selectEntry->ind.node_num; f++)
    { 
        FBT[selectEntry->ind.fat[f].pos]=0;  //该块置为空闲
    }
    char content[MAX_DATANODE_NUM][MAX_BLOCK_SIZE];
    char tmp;
    inode ind;
    printf("内容请以'$'结束！\n");
    int i = 0;
    while((tmp=getchar())!='$')
    {
        if(i==0&&tmp=='\n')
        {
            continue;
        }
        content[i/MAX_BLOCK_SIZE][i++] = tmp;
    }
    ind.size = i;
    //此时已经结束输入
    if(i>(MAX_BLOCK_SIZE-1)*MAX_DATANODE_NUM)
    {
        printf("文件过大，无法存储，创建失败 !\n");
        return;
    }
    //数组内容分块
    int k;
    for(k=0; k<=i/(MAX_BLOCK_SIZE-1); k++)
    {
        block bk;
        int bkn;
        for(bkn=0; bkn<MAX_BLOCK_SIZE-1; bkn++)
        {
            bk.content[bkn]='$';
        }
        bk.content[MAX_BLOCK_SIZE-1]='\0';
        char *tmp;
        int tp=0;
        int len=0;
        if(k==0)  //第一个数据块
        {
            if(i<MAX_BLOCK_SIZE-1)
            {
                len = i;
            }
        }
        if(k==1)
        {
            len=i%(MAX_BLOCK_SIZE-1)+1;
        }
        for(tp=0; tp<len; tp++)
        {
            bk.content[tp] = content[k][tp];
        }
        bk.isRevised = true;
        if(k==0)
        {
            bk.pos = getFreeBlock();
        }
        else
        {
            bk.pos = getFreeBlock();
            if(bk.pos==-1)
            {
                printf("数据块已用完,内存不足!\n");
                return;
            }
        }
        saveBlock(bk);
        ind.fat[k].pos = bk.pos;  //该块数据块使用情况
        ind.fat[k].begin = 0;
        ind.fat[k].end = len;
    }
    ind.node_num = k;
    strcpy(ind.code, selectEntry->ind.code);
    strcpy(ind.filename, selectEntry->ind.filename);
    ind.num = selectEntry->ind.node_num;
    selectEntry->ind = ind;
    saveDir();
    saveFBT();
    printf("文件已保存 !\n");
}
/*追加到文件末尾*/
void op_append()
{
    char tmp[MAX_BLOCK_SIZE*2];
    char ch;
    printf("内容请以'$'结束！\n");
    int i = 0;
    while((ch=getchar())!='$')
    {
        if(i==0&&ch=='\n')
        {
            continue;
        }
        tmp[i++] = ch;
    }
    tmp[i]='\0';
    //此时已经完成输入
    if((i+selectEntry->ind.size)>(MAX_BLOCK_SIZE-1)*MAX_DATANODE_NUM)
    {
        printf("文件过大，无法存储，创建失败 !\n");
        return;
    }
    else
    {
        if(selectEntry->ind.size>MAX_BLOCK_SIZE-1)               //占用了两个block
        {
            int offset = selectEntry->ind.size - MAX_BLOCK_SIZE + 1;
            FILE* bfp = fopen(blockspath,"r+");
            if(bfp==NULL)
            {
                printf("磁盘发生错误！\n");
                return;
            }
            else
            {
                fseek(bfp,(selectEntry->ind.fat[1].pos*(MAX_BLOCK_SIZE-1)+offset),SEEK_SET);  //在第二个磁盘里增加内容
                fwrite(tmp,sizeof(char),i,bfp);
                fclose(bfp);
                selectEntry->ind.size = selectEntry->ind.size + i;
                selectEntry->ind.fat[1].end = selectEntry->ind.fat[1].end + i;
                saveDir();
                printf("文件保存成功 !\n");
            }
        }
        else            //只占用了一个block
        {
            if(i<(MAX_BLOCK_SIZE-1-selectEntry->ind.size)) //不会占用新的block
            {
                FILE* bfp = fopen(blockspath,"r+");
                if(bfp==NULL)
                {
                    printf("磁盘发生错误！\n");
                    return;
                }
                else
                {
                    fseek(bfp,(selectEntry->ind.fat[0].pos*(MAX_BLOCK_SIZE-1)+selectEntry->ind.size),SEEK_SET); //在第一个磁盘里增加内容
                    fwrite(tmp,sizeof(char),i,bfp);
                    fclose(bfp);
                    selectEntry->ind.size = selectEntry->ind.size + i;
                    selectEntry->ind.fat[0].end = selectEntry->ind.fat[0].end + i;
                    saveDir();
                    printf("文件保存成功 !\n");
                }
            }
            else        //要占用新的block
            {
                int bkNum = getFreeBlock();
                if(bkNum==-1)
                {
                    printf("数据块已用完,内存不足!\n");
                    return;
                }
                char *p1 = (char*)malloc((MAX_BLOCK_SIZE-1-selectEntry->ind.size)*sizeof(char));
                char *p2 = (char*)malloc((i-(MAX_BLOCK_SIZE-1-selectEntry->ind.size))*sizeof(char));
                int pi;
                int pn1=0,pn2=0;
                for(pi=0; pi<i; pi++)
                {
                    if(pi<MAX_BLOCK_SIZE-1-selectEntry->ind.size)
                    {
                        p1[pn1++] = tmp[pi];
                    }
                    else
                    {
                        p2[pn2++] = tmp[pi];
                    }
                }
                p1[pn1] = '\0';
                p2[pn2] = '\0';
                //存储
                FILE *bfp = fopen(blockspath, "r+");
                if(bfp==NULL)
                {
                    printf("磁盘发生错误！\n");
                    return;
                }
                else
                {
                    fseek(bfp,((MAX_BLOCK_SIZE-1)*selectEntry->ind.fat[0].pos+selectEntry->ind.fat[0].end),SEEK_SET);
                    fwrite(p1,sizeof(char),pn1,bfp);
                    fseek(bfp,((MAX_BLOCK_SIZE-1)*bkNum),SEEK_SET);
                    fwrite(p2,sizeof(char),pn2,bfp);
                    fclose(bfp);
                    FBT[bkNum]=1;
                    selectEntry->ind.node_num = 2;
                    selectEntry->ind.size = selectEntry->ind.size + i;
                    selectEntry->ind.fat[0].end = MAX_BLOCK_SIZE-2;
                    selectEntry->ind.fat[1].pos = bkNum;
                    selectEntry->ind.fat[1].begin = 0;
                    selectEntry->ind.fat[1].end = pn2;
                    saveFBT();
                    saveDir();
                    printf("文件保存成功 !\n");
                }
            }
        }
    }
}
/*----------查看文件内容--------*/
void nread()
{
    FILE* bfp = fopen(blockspath,"r");
    if(bfp==NULL)
    {
        printf("不存在磁盘文件 !\n");
        while(1);
    }
    else             //打开磁盘文件
    {
        int i;
        char tmp = ' ';
        printf("文件[%s]中的内容如下:\n",selectEntry->ind.filename);
        if(selectEntry->ind.size==0)
        {
            printf("内容为空。\n");
        }
        else
        {
            for(i=0; i<selectEntry->ind.node_num; i++)
            {
                fseek(bfp,(selectEntry->ind.fat[i].pos*(MAX_BLOCK_SIZE-1)),SEEK_SET);        //定位到指定位置
                int j;
                for(j=selectEntry->ind.fat[i].begin; j<selectEntry->ind.fat[i].end; j++)
                {
                    tmp = fgetc(bfp);
                    printf("%c",tmp);
                }
            }
            printf("\n");
        }
        fclose(bfp);
    }
}
/*------------关闭文件-----------*/
void nclose()
{
    selectEntry==NULL;
    printf("文件已关闭 !\n");
}

int main()
{
    printf("欢迎使用虚拟文件管理系统\n");
    FILE *ufp = fopen(userspath, "r");         //检查是否第一次使用本系统，查看是否存在users文件
    if(ufp==NULL)
    {
        printf("初次使用本系统....\n");
        printf("正在创建数据目录...\n");
        char *datapath = "userdata";
        mkdir(datapath,0777);
        FILE *fp = fopen(userspath,"w");
        createBlocksDisk();                          //第一次使用，开辟磁盘空间
        if(fp==NULL)
        {
            printf("初始化出现错误！\n");
            return 0;
        }
        else
        {
            fclose(fp);
        }
    }
    else
    {
        fclose(ufp);
    }
    while(LgRg());
    printf("已进入虚拟文件管理系统\n");
    dirpath = getDirpath(currUser.account);
    initDir(getDirpath(currUser.account));
    initFBT();
    printf("输入help查看命令帮助,输入exit退出系统\n");
    while(1)
    {
        char cmd[10];
        printf(">>> ");
        scanf("%s",cmd);
        if(strcmp(cmd, "exit")==0)
        {
            return 0;
        }
        else if(strcmp(cmd, "help")==0)
        {
            help();
        }
        else if(strcmp(cmd, "dir")==0)
        {
            dir();
        }
        else if(strcmp(cmd, "create")==0)
        {
            create();
        }
        else if(strcmp(cmd, "delete")==0)
        {
            del();
        }
        else if(strcmp(cmd,"open")==0)
        {
            open();
        }
        else if(strcmp(cmd, "read")==0)
        {
            printf("请先打开文件！\n");
        }
        else if(!strcmp(cmd, "write")==0)
        {
            printf("请先打开文件！\n");
        }
        else if(!strcmp(cmd, "close")==0)
        {
            printf("请先打开文件！\n");
        }
        else
        {
            printf("无效的指令！\n");
        }
    }
    return 0;
}

