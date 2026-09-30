
#include "list.h"
#include <stdlib.h>
#include <string.h>
//创建链表头节点
node * initList()
{
    //1.申请头节点的空间    
    node * head = malloc(sizeof(node));
    
    //2.指针域进行初始化
    //头节点的next指向NUL   因为目前链表有且只有一个节点(头节点)
    //头节点的数据域是无效的,不用赋值  不管它
    head->next = NULL;
    
    return head;
}

//创建一个新节点
node * create_newnode(char inputData[])
{
    //1.申请头节点的空间    
    node * new_node = malloc(sizeof(node));
    
    //2.新节点的数据域和指针域进行初始化
    strcpy(new_node->bmp_pathname,inputData);
    new_node->next = NULL;
    
    return new_node;
}

//将新节点 new插入到链表的首部
void insert(node *head,node *newnode)
{
    newnode->next = head->next;
    head->next = newnode;
}

//判断链表是不是空的
bool isEmpty(node *head)
{
    return head->next == NULL;    
}

//将链表从头部删除
node * Remove(node *head)
{
    if( isEmpty(head))
        return NULL;
    node * temp = head->next;
    
    //将原链表的首节点绕过
    head->next = temp->next;
    temp->next = NULL;
    
    return temp;
}

//遍历链表
void listForEach(node * head)
{
    if(isEmpty(head))
    {
        printf("链表是空的,不遍历\n");
        return;
    }
    for(node * tmp = head->next;tmp!=NULL;tmp = tmp->next)
    {
        printf("%s\t",tmp->bmp_pathname);    
    }printf("\n");
}

void destroy(node * head)
{
    if(isEmpty(head))
        return ;    
    node * n;
    for(node * tmp = head->next;tmp != NULL;tmp = n)
    {
        n = tmp->next;
        free(tmp);    
    }
}