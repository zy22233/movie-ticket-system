#ifndef __LIST_H__
#define __LIST_H__

#include <stdio.h>
#include <stdbool.h>


typedef struct node{
    //以整型数据为例子
    char bmp_pathname[1024];
    //指向相邻的下一个节点的指针
    struct node * next;    //next 下一个  prev 上一个
}node;

//创建空链表
node * initList();
//创建新节点
node * create_newnode(char inputData[]);
//将新节点 new插入到链表的首部
void insert(node *head,node *newnode);
//将链表从头部删除
node * Remove(node *head);
//遍历链表
void listForEach(node * head);
//销毁链表
void destroy(node * head);


#endif