/*
    qqaccount.cpp - Kopete QQ Account

    Copyright (c) 2014      by Jun Zhang		     <jun.zhang@i-soft.com.cn>
    Copyright (c) 2003      by Will Stephenson		 <will@stevello.free-online.co.uk>
    Kopete    (c) 2002-2003 by the Kopete developers <kopete-devel@kde.org>

    *************************************************************************
    *                                                                       *
    * This library is free software; you can redistribute it and/or         *
    * modify it under the terms of the GNU General Public                   *
    * License as published by the Free Software Foundation; either          *
    * version 2 of the License, or (at your option) any later version.      *
    *                                                                       *
    *************************************************************************
*/

#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>

#include <kaction.h>
#include <kdebug.h>
#include <klocale.h>
#include <kactionmenu.h>
#include <kmenu.h>
#include <kicon.h>

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <libgen.h>
#include <pthread.h>

// KDE
#include <klocale.h>
#include <kconfig.h>
#include <kdebug.h>
#include <kaction.h>
#include <kactionmenu.h>
#include <kmenu.h>
#include <kmessagebox.h>
#include <krun.h>
#include <kstandarddirs.h>
#include <ktoolinvocation.h>
#include <kicon.h>
#include <knotification.h>

#include <QImage>
#include <QBuffer>
#include <QFile>
#include <QList>
// Kopete
#include <kopetegroup.h>
//#include "kopetedeletecontacttask.h"
#include <kopetechatsession.h>
#include <kopetemessage.h>
#include <kopetepassword.h>
#include <kopeteuiglobal.h>
#include <knotification.h>
#include <kopetemetacontact.h>
#include <kopetecontactlist.h>
#include <kopetetransfermanager.h>
#include <kopeteview.h>
#include <kopeteaddedinfoevent.h>
#include "kopeteavatarmanager.h"

#include "qqcontact.h"
#include "qqprotocol.h"

#include "qqloginverifywidget.h"

#include "qq_types.h"
#include "translate.h"

#include "qqaccount.h"
#include "qqshowgetinfo.h"
#include "qquserinfoform.h"

#define OPEN_URL(var,url) snprintf(var,sizeof(var),"xdg-open '%s'",url);
#define GLOBAL_HASH_JS() SHARE_DATA_DIR"/hash.js"
#define LOCAL_HASH_JS(buf)  (snprintf(buf,sizeof(buf),"%s/hash.js",\
    lwdb_get_config_dir()),buf)
static int g_ref_count = 0;

QQAccount::QQAccount( WebqqProtocol *parent, const QString& accountID )
    : Kopete::PasswordedAccount ( parent, accountID, false)
{	
    m_protocol = parent;
    m_lc = NULL;
    // Init the myself contact
    setMyself( new QQContact( this, accountId(), accountId(), Kopete::ContactList::self()->myself() ) );
    myself()->setOnlineStatus( m_protocol->WebqqOffline );

    this->cleanAll_contacts();
    m_targetStatus = Kopete::OnlineStatus::Offline;

    qRegisterMetaType<CallbackObject>("CallbackObject");

    QObject::connect( ObjectInstance::instance(), SIGNAL(signal_from_instance(CallbackObject)),
                      this,SLOT(slotReceivedInstanceSignal(CallbackObject)) );

}

QQAccount::~QQAccount()
{
    //delete m_server;
}

void QQAccount::fillActionMenu( KActionMenu *actionMenu )
{
    Kopete::Account::fillActionMenu( actionMenu );

    actionMenu->addSeparator();

    KAction *action;

    action = new KAction (KIcon("qq_showvideo"), i18n ("Show my own video..."), actionMenu );
    //, "actionShowVideo");
    QObject::connect( action, SIGNAL(triggered(bool)), this, SLOT(slotShowVideo()) );
    actionMenu->addAction(action);
    action->setEnabled( isConnected() );

    KAction *nickAction = new KAction (KIcon("qq_changeNick"), i18n ("change LongNick"), actionMenu );
    QObject::connect(nickAction, SIGNAL(triggered(bool)), this, SLOT(slotChangeNick()));
    actionMenu->addAction(nickAction);
    nickAction->setEnabled(isConnected());

}

void QQAccount::slotChangeNick()
{
    LwqqAsyncEvent* ev = lwqq_info_get_single_long_nick(m_lc, m_lc->myself);
    lwqq_async_add_event_listener(ev, _C_(p,display_self_longnick,m_lc));
}


void QQAccount::setAway( bool away, const QString & /* reason */ )
{
    if ( away )
        slotGoAway();
    else
        slotGoOnline();
}

void QQAccount::setOnlineStatus(const Kopete::OnlineStatus& status, const Kopete::StatusMessage &reason, const OnlineStatusOptions& /*options*/)
{
    if ( status.status() == Kopete::OnlineStatus::Online &&
         myself()->onlineStatus().status() == Kopete::OnlineStatus::Offline )
    {
        m_targetStatus = Kopete::OnlineStatus::Online;
        slotGoOnline();
    }
    else if (status.status() == Kopete::OnlineStatus::Online &&
             (myself()->onlineStatus().status() == Kopete::OnlineStatus::Away ||
              myself()->onlineStatus().status() == Kopete::OnlineStatus::Busy||
              myself()->onlineStatus().status() == Kopete::OnlineStatus::Invisible) )
    {
        m_targetStatus = Kopete::OnlineStatus::Online;
        setAway( false, reason.message() );
    }
    else if ( status.status() == Kopete::OnlineStatus::Offline )
        slotGoOffline();
    else if ( status.status() == Kopete::OnlineStatus::Away )
    {
        m_targetStatus = Kopete::OnlineStatus::Away;
        slotGoAway( /* reason */ );
    }
    else if ( status.status() == Kopete::OnlineStatus::Busy )
    {
        m_targetStatus = Kopete::OnlineStatus::Busy;
        slotGoBusy( /* reason */ );
    }
    else if ( status.status() == Kopete::OnlineStatus::Invisible )
    {
        m_targetStatus = Kopete::OnlineStatus::Invisible;
        slotGoHidden( );
    }
}

void QQAccount::init_client_events(LwqqClient *lc)
{
    lwqq_add_event(lc->events->login_complete,
                   _C_(2p,cb_login_stage_1,lc, &lc->args->login_ec));
    lwqq_add_event(lc->events->new_friend,
                   _C_(2p,cb_friend_come,lc, &lc->args->buddy));
    lwqq_add_event(lc->events->new_group,
                   _C_(2p,cb_group_come,lc, &lc->args->group));
    lwqq_add_event(lc->events->poll_msg,
                   _C_(p,cb_qq_msg_check,lc));
    lwqq_add_event(lc->events->poll_lost,
                   _C_(p,cb_lost_connection,lc));
    lwqq_add_event(lc->events->need_verify,
                   _C_(2p,cb_need_verify2,lc, &lc->args->vf_image));
    lwqq_add_event(lc->events->delete_group,
                   _C_(2p,cb_delete_group_local,lc, &lc->args->deleted_group));
    lwqq_add_event(lc->events->group_member_chg,
                   _C_(2p,cb_flush_group_members,lc, &lc->args->group));
    lwqq_add_event(lc->events->upload_fail,
                   _C_(4p,cb_upload_content_fail,lc, &lc->args->serv_id, &lc->args->content, &lc->args->err));
}

void QQAccount::initLwqqAccount()
{
    if(m_lc)
        return;

    kDebug(WEBQQ_GEN_DEBUG)<<"init lwqq account\n";


    char* username = s_strdup(accountId().toAscii().constData());
    char* password = "XXXX";
    qq_account* ac = qq_account_new(username, password);
    g_ref_count ++ ;
    m_lc = ac->qq;
    init_client_events(ac->qq);

    ac->db = lwdb_userdb_new(username,NULL,0);

    LwqqExtension* db_ext = lwdb_make_extension(ac->db);
    db_ext->init(ac->qq, db_ext);
    ac->qq->data = ac;
    lwqq_get_http_handle(ac->qq)->ssl = 1;
    //for empathy
    lwqq_bit_set(ac->flag,QQ_USE_QQNUM,ac->db!=NULL);
    if(ac->db){
        const char* url = "http://pidginlwqq.sinaapp.com/statics.php";
        char post[128];
        snprintf(post,sizeof(post),"v=0.3.1");
        LwqqHttpRequest *req = lwqq_http_request_new(url);
        req->lc = ac->qq;
        req->do_request_async(req,1,post,_C_(p,lwqq_http_request_free,req));
        lwqq_override(ac->font.family,s_strdup(lwdb_userdb_read(ac->db, "f_family")));
        ac->font.size = s_atoi(lwdb_userdb_read(ac->db,"f_size"),ac->font.size);
        ac->font.style = s_atoi(lwdb_userdb_read(ac->db,"f_style"),ac->font.style);

        const char* last_hash = lwdb_userdb_read(ac->db, "last_hash");
        if(last_hash) lwqq_hash_set_beg(ac->qq, last_hash);
    }

}

void QQAccount::queryResult(KJob *job)
{
    fprintf(stderr, "queryResult\n");
    Kopete::AvatarQueryJob *queryJob = static_cast<Kopete::AvatarQueryJob*>(job);
    if( !queryJob->error() )
    {
        QList<Kopete::AvatarManager::AvatarEntry> avatarList = queryJob->avatarList();
        fprintf(stderr, "count:%d\n", avatarList.count());
        foreach(const Kopete::AvatarManager::AvatarEntry &entry, avatarList)
        {
            fprintf(stderr, "AvatarEntry name:%s\n", entry.name.toUtf8().data());
        }
    }
}

void QQAccount::destoryLwqqAccount()
{

    printf("will close qq\n");
    if (m_lc == NULL)
        return;
    qq_account* ac = (qq_account *)(m_lc->data);

    if(lwqq_client_logined(ac->qq))
        lwqq_logout(ac->qq,3);
    lwqq_msglist_close(ac->qq->msg_list);
    m_lc = NULL;
    lwdb_userdb_free(ac->db);
    qq_account_free(ac);
    translate_global_free();
    g_ref_count -- ;
    if(g_ref_count == 0){
        lwqq_http_global_free(LWQQ_CLEANUP_IGNORE);
        lwqq_async_global_quit();
        lwdb_global_free();
    }
}

int QQAccount::login()
{
    LwqqErrorCode err;

    lwqq_login(m_lc,LWQQ_STATUS_ONLINE, &err);

    return 0;
}

void QQAccount::logout()
{
    LwqqErrorCode err;

    lwqq_logout(m_lc, &err);
    if (err != LWQQ_EC_OK) {
        kWarning(14210) << "logout failed"<<endl;
    } else {
        kWarning(14210) << "loginout success"<<endl;
    }
}



void QQAccount::slotReceivedInstanceSignal(CallbackObject cb)
{
    /*call need_verify2 function*/
    if(cb.fun_t == NEED_VERIFY2)
    {
        LwqqClient* lc = (LwqqClient*)(cb.ptr1);
        LwqqVerifyCode* code = (LwqqVerifyCode*) (cb.ptr2);
        this->ac_need_verify2(lc, code);
    }
    else if(cb.fun_t == LOGIN_COMPLETE)
    {
        LwqqClient* lc = (LwqqClient*)(cb.ptr1);
        LwqqErrorCode *err = (LwqqErrorCode *)(cb.ptr2);
        this->ac_login_stage_1(lc, err);
    }
    else if(cb.fun_t == LOGIN_STAGE_2)
    {        
        LwqqAsyncEvent *ev = (LwqqAsyncEvent *)(cb.ptr1);
        LwqqClient* lc = (LwqqClient*)(cb.ptr2);
        this->ac_login_stage_2(ev,lc);
    }
    else if(cb.fun_t == LOGIN_STAGE_3)
    {
        LwqqClient* lc = (LwqqClient*)(cb.ptr1);
        this->ac_login_stage_3(lc);
    }
    else if(cb.fun_t == LOGIN_STAGE_f)
    {
        LwqqClient* lc = (LwqqClient*)(cb.ptr1);
        this->ac_login_stage_f(lc);
    }
    else if (cb.fun_t == FRIEND_AVATAR)
    {
        LwqqClient *lc = (LwqqClient*)(cb.ptr1);
        LwqqBuddy *buddy = (LwqqBuddy*)(cb.ptr2);
        this->ac_friend_avatar(lc, buddy);
    }
    else if(cb.fun_t == GROUP_AVATAR)
    {
        LwqqClient *lc = (LwqqClient*)(cb.ptr1);
        LwqqGroup *group = (LwqqGroup*)(cb.ptr2);
        this->ac_group_avatar(lc, group);
    }
    else if(cb.fun_t == GROUP_MEMBERS)
    {
        LwqqClient *lc = (LwqqClient*)(cb.ptr1);
        LwqqGroup *group = (LwqqGroup*)(cb.ptr2);
        this->ac_group_members(lc, group);
    }
    else if (cb.fun_t == QQ_MSG_CHECK)
    {
        LwqqClient *lc = (LwqqClient*)(cb.ptr1);
        this->ac_qq_msg_check(lc);
    }else if(cb.fun_t == SHOW_CONFIRM)
    {
        LwqqClient *lc = (LwqqClient*)(cb.ptr1);
        LwqqConfirmTable *table = (LwqqConfirmTable*)(cb.ptr2);
        add_info *info = (add_info*)(cb.ptr3);
        this->ac_show_confirm_table(lc, table, info);
    }else if(cb.fun_t == SHOW_MESSAGE)
    {
        msg_type type = cb.type;
        const char *title = (const char*)(cb.ptr1);
        const char *msg = (const char *)(cb.ptr2);
        this->ac_show_messageBox(type, title, msg);
    }else if(cb.fun_t == FRIEND_COME)
    {
        LwqqClient *lc = (LwqqClient*)(cb.ptr1);
        LwqqBuddy *buddy = (LwqqBuddy*)(cb.ptr2);
        this->ac_friend_come(lc, buddy);
    }else if(cb.fun_t == REWRITE_MSG)
    {
        LwqqAsyncEvent* ev = (LwqqAsyncEvent*)(cb.ptr1);
        qq_account* ac = (qq_account*)(cb.ptr2);
        LwqqGroup* group = (LwqqGroup*)(cb.ptr3);
        this->ac_rewrite_whole_message_list(ev, ac, group);
    }else if(cb.fun_t == SET_GROUPNAME)
    {
        LwqqGroup* group = (LwqqGroup*)(cb.ptr1);
        this->ac_qq_set_group_name(group);
    }else if(cb.fun_t == GET_USERINFO)
    {
        qq_account* ac = (qq_account*)(cb.ptr1);
        LwqqBuddy* buddy = (LwqqBuddy*)(cb.ptr2);
        char *who = (char*)(cb.ptr3);
        this->ac_display_user_info(ac, buddy, who);
    }


    /*add function here*/

    /*
  else if(cb.fun_t == XXX)
  {
  }
  */

}

bool QQAccount::createContact(const QString &contactId, Kopete::MetaContact *parentContact)
{
    if(!contact(contactId))
    {
        // FIXME: New Contacts are NOT added to KABC, because:
        // How on earth do you tell if a contact is being deserialised or added brand new here?
        // -- actualy (oct 2004) this method is only called when new contact are added.  but this will
        //    maybe change and you will be noticed   --Olivier
        QQContact *newContact = new QQContact( this, contactId,
                                               parentContact->displayName(), parentContact );
        return newContact != 0;
    }
    else
        kDebug(WEBQQ_GEN_DEBUG) << "Contact already exists";

    return false;
}

bool QQAccount::createChatSessionContact(const QString &id, const QString &name)
{
    Kopete::MetaContact *m = new Kopete::MetaContact;
    m->setTemporary( true );
    m->setDisplayName(name);
    if(!contact(id))
    {
        // FIXME: New Contacts are NOT added to KABC, because:
        // How on earth do you tell if a contact is being deserialised or added brand new here?
        // -- actualy (oct 2004) this method is only called when new contact are added.  but this will
        //    maybe change and you will be noticed   --Olivier
        QQContact *newContact = new QQContact( this, id,
                                               name, m );
        return newContact != 0;
    }
    else
        kDebug(WEBQQ_GEN_DEBUG) << "Contact already exists";

    return false;
}


void QQAccount::buddy_message(LwqqClient* lc,LwqqMsgMessage* msg)
{
    qq_account* ac = (qq_account*)lwqq_client_userdata(lc);
    char buf[BUFLEN]={0};
    //clean buffer
    strcpy(buf,"");
    LwqqBuddy* buddy = msg->buddy.from;
    translate_struct_to_message(ac,msg,buf);

    /*find the contact*/
    QQContact * contact = dynamic_cast<QQContact *>(contacts().value( QString(buddy->qqnumber) ));
    if ( !contact)
        return;
    
    contact->receivedMessage(stransMsg(QString::fromUtf8(buf)));
    
}

QString QQAccount::stransMsg(const QString &message)
{
    QString reMsg;
    int pos = 0;
    int endPos = 0;
    int size = 0;
    int msgPos = 0;

    reMsg = "<span style=\" ";
    if((pos = message.indexOf("face=\"")) >= 0)
    {
        size = QString("face=\"").size();
        endPos = message.indexOf("\"", pos + size);
        reMsg +=" font-family:'" + message.mid(pos + size, endPos - pos - size ) + "';";
        msgPos = pos;
    }
    if(message.indexOf("<b>") >= 0)
        reMsg += "font-weight:600;";
    if(message.indexOf("<i>") >= 0)
        reMsg += " font-style:italic;";
    if(message.indexOf("<u>") >= 0)
        reMsg += " text-decoration: underline;";
    if((pos = message.indexOf("color=\"#")) >= 0)
    {
        size = QString("color=\"#").size();
        reMsg += " color:#" + message.mid(pos+size, 6) + ";" ;
        msgPos = pos;
    }

    if((pos = message.indexOf("size=\"")) >= 0)
    {
        size = QString("size=\"").size();
        endPos = message.indexOf("\"", pos + size);
        reMsg +=" font-size:" + message.mid(pos + size, endPos - pos - size ) + "pt;";
        msgPos = pos;
    }
    reMsg += "\">";
    if((pos = message.indexOf(">", msgPos)) >= 0)
    {
        endPos = message.indexOf("</font>", pos);
        size = QString(">").size();
        reMsg +=message.mid(pos + size, endPos - pos - size ) + "</span>";
    }

    return reMsg;
}

void QQAccount::group_message(LwqqClient *lc, LwqqMsgMessage *msg)
{
    qq_account* ac =(qq_account*) lwqq_client_userdata(lc);
    LwqqGroup* group = find_group_by_gid(lc,(msg->super.super.type==LWQQ_MS_DISCU_MSG)?msg->discu.did:msg->super.from);
    Kopete::ContactPtrList justMe;
    LwqqBuddy* buddy;
    QString sendId;
    if(group == NULL) return;

    if(LIST_EMPTY(&group->members) || group->mask > 0)
    {
        receivedGroupMessage(lc, group, msg);
    }else{
        QString id = QString(group_is_qun(group)?group->gid:group->did);
        if(m_msgMap[id].size() > 0)
        {
            rewrite_group_msg(id);
        }
        //force open dialog
        static char buf[BUFLEN] ;
        strcpy(buf,"");

        translate_struct_to_message(ac,msg,buf);
        /*find the contact*/
        QQContact * chatContact = dynamic_cast<QQContact *>(contacts().value( id ));
        if ( !chatContact)
            return;
        justMe.append(myself());
        QString sendMsg = stransMsg(QString::fromUtf8(buf));
        if((buddy = find_buddy_by_uin(lc,msg->group.send)))
        {
            if(buddy->qqnumber)
                sendId = QString(buddy->qqnumber);
            else
                sendId = QString(msg->group.send);
        }
        QDateTime msgDT;
        msgDT.setTime_t(msg->time);
        Kopete::Message kmsg( contact(sendId), justMe );
        //kDebug(WEBQQ_GEN_DEBUG)<<"group message:"<<sendMsg;
        kmsg.setTimestamp( msgDT );
        kmsg.setHtmlBody(sendMsg);
        kmsg.setDirection( Kopete::Message::Inbound );
        chatContact->setContactType(Contact_Group);
        chatContact->manager(Kopete::Contact::CanCreate)->appendMessage(kmsg);
    }
}

void QQAccount::slotGetUserInfo(QString id, ConType type)
{
    qq_account *ac = (qq_account*)(m_lc->data);
    if(type == Contact_Chat)
    {
        LwqqBuddy* buddy;
        buddy = find_buddy_by_qqnumber(m_lc,id.toUtf8().constData());
        if(buddy){
            LwqqAsyncEvent* ev = lwqq_info_get_friend_detail_info(m_lc, buddy);
            lwqq_async_add_event_listener(ev, _C_(3p,cb_display_user_info,ac,buddy,s_strdup(id.toUtf8().constData())));
        }
    }else if(type == Contact_Session)
    {
        LwqqGroup* g = NULL;
        LwqqSimpleBuddy* sb = NULL;
        if(find_group_and_member_by_gid(m_lc, id.toUtf8().constData(), &g, &sb))
        {
            LwqqBuddy* buddy = lwqq_buddy_new();
            buddy->uin = s_strdup(sb->uin);

            LwqqAsyncEvset* set = lwqq_async_evset_new();
            LwqqAsyncEvent* ev = NULL;
            ev = lwqq_info_get_group_member_detail(m_lc,sb->uin,buddy);
            lwqq_async_evset_add_event(set, ev);
            ev = lwqq_info_get_friend_qqnumber(m_lc,buddy);
            lwqq_async_evset_add_event(set, ev);
            lwqq_async_add_evset_listener(set, _C_(3p,cb_display_user_info,ac,buddy, s_strdup(id.toUtf8().constData())));
        }
    }
}

void QQAccount::ac_display_user_info(qq_account *ac, LwqqBuddy *b, char *who)
{
    char uin[128]={0};
    char gid[128]={0};
    const char* pos;
    QString id;
    if((pos = strstr(who," ### "))!=NULL) {
        strcpy(gid,pos+strlen(" ### "));
        strncpy(uin,who,pos-who);
        uin[pos-who] = '\0';
        id = QString(uin);
    }else{
        id = QString(who);
        lwdb_userdb_update_buddy_info(ac->db, &b);
    }
    QQUserInfoForm *info = new QQUserInfoForm(contact(id));
    info->setInfo(b);
    info->show();
}

void QQAccount::receivedGroupMessage(LwqqClient *lc, LwqqGroup *group, LwqqMsgMessage *msg)
{
    kDebug(WEBQQ_GEN_DEBUG)<<"receivedGroupMessage";
    qq_account *ac = lwqq_client_userdata(lc);
    if(LIST_EMPTY(&group->members))
    {
        LwqqAsyncEvent* ev = lwqq_async_queue_find(&group->ev_queue,(void*)lwqq_info_get_group_detail_info);
        if(ev == NULL){
            ev = lwqq_info_get_group_detail_info(m_lc,group,NULL);
            lwqq_async_add_event_listener(ev,_C_(3p,cb_rewrite_whole_message_list,ev,ac,group));
        }
    }
    char buf[BUFLEN] ;
    strcpy(buf,"");
    translate_struct_to_message(ac,msg,buf);
    group_msg *message = (group_msg*)s_malloc0(sizeof(group_msg));
    message->send_id = s_strdup(msg->group.send);
    message->when = msg->time;
    message->what = s_strdup(buf);
    QString id = QString(group->type == LWQQ_GROUP_QUN?group->gid:group->did);
    MsgDataList msgList = m_msgMap[id];
    msgList.append(*message);
    m_msgMap.insert(id, msgList);
    if(group->mask > 0)
        ac_qq_set_group_name(group);
}

void QQAccount::rewrite_group_msg(const QString &id)
{
    group_msg *message;
    LwqqBuddy* buddy;
    QString sendId;
    MsgDataList msgList = m_msgMap[id];
    LwqqGroup* group = find_group_by_gid(m_lc,id.toUtf8().constData());
    /*find the contact*/
    QQContact * chatContact = dynamic_cast<QQContact *>(contacts().value( id ));
    if ( !chatContact)
        return;
    for(int i = 0; i < msgList.size(); i++)
    {
        message = &msgList.at(i);
        if(message == NULL)
        {
            kDebug(WEBQQ_GEN_DEBUG)<<"null";
            return;
        }
        Kopete::ContactPtrList justMe;
        justMe.append(myself());
        QString sendMsg = stransMsg(QString::fromUtf8(message->what));
        if((buddy = find_buddy_by_uin(m_lc,message->send_id)))
        {
            if(buddy->qqnumber)
                sendId = QString(buddy->qqnumber);
            else
                sendId = QString(message->send_id);
        }
        QDateTime msgDT;
        msgDT.setTime_t(message->when);
        Kopete::Message kmsg( contact(sendId), justMe );
        kDebug(WEBQQ_GEN_DEBUG)<<"group message:";
        kmsg.setTimestamp( msgDT );
        kmsg.setHtmlBody(sendMsg);
        kmsg.setDirection( Kopete::Message::Inbound );
        chatContact->setContactType(Contact_Group);
        chatContact->manager(Kopete::Contact::CanCreate)->appendMessage(kmsg);
    }
    m_msgMap[id].clear();
    ac_qq_set_group_name(group);
}

void QQAccount::ac_rewrite_whole_message_list(LwqqAsyncEvent *ev, qq_account *ac, LwqqGroup *group)
{
    kDebug(WEBQQ_GEN_DEBUG)<<"ac_rewrite_whole_message_list";
    if(ev->result != LWQQ_EC_OK) return;
    ac_group_members(m_lc, group);
    if(group->mask == 0)
    {
        group_msg *message;
        LwqqBuddy* buddy;
        QString sendId;
        QString id = QString(group->type == LWQQ_GROUP_QUN?group->gid:group->did);
        MsgDataList msgList = m_msgMap[id];
        /*find the contact*/
        QQContact * chatContact = dynamic_cast<QQContact *>(contacts().value( id ));
        if ( !chatContact)
            return;
        for(int i = 0; i < msgList.size(); i++)
        {
            message = &msgList.at(i);
            Kopete::ContactPtrList justMe;
            justMe.append(myself());
            QString sendMsg = stransMsg(QString::fromUtf8(message->what));
            if((buddy = find_buddy_by_uin(m_lc,message->send_id)))
            {
                if(buddy->qqnumber)
                    sendId = QString(buddy->qqnumber);
                else
                    sendId = QString(message->send_id);
            }
            QDateTime msgDT;
            msgDT.setTime_t(message->when);
            Kopete::Message kmsg( contact(sendId), justMe );
            //kDebug(WEBQQ_GEN_DEBUG)<<"group message:"<<sendMsg;
            kmsg.setTimestamp( msgDT );
            kmsg.setHtmlBody(sendMsg);
            kmsg.setDirection( Kopete::Message::Inbound );
            chatContact->setContactType(Contact_Group);
            chatContact->manager(Kopete::Contact::CanCreate)->appendMessage(kmsg);
        }
        m_msgMap[id].clear();
    }
}

void QQAccount::ac_qq_set_group_name(LwqqGroup *group)
{
    char gname[256] = {0};
    int hide = group->mask;
    QString id = QString(group_is_qun(group)?group->gid:group->did);
    const char* left = group->mask==2? "((":"(";
    const char* right = group->mask==2?"))":")";
    if(hide) strcat(gname,left);
    strcat(gname,group->markname?:group->name?:group->account);
    if(hide){
        strcat(gname,right);
        if(hide==1){
            unsigned int unread = m_msgMap[id].size();
            unsigned int split = unread<10?unread:unread<100?unread/10*10:unread/100*100;
            if(unread>0)sprintf(gname+strlen(gname), "(%u%s)",split,unread>10?"+":"");
        }
    }
    if(contact(id))
    {
        contact(id)->set_group_name(QString::fromUtf8(gname));
        contact(id)->metaContact()->setDisplayName(QString::fromUtf8(gname));
    }
}

bool QQAccount::group_is_qun(LwqqGroup *group)
{
    return (group->type==LWQQ_GROUP_QUN);
}

void QQAccount::whisper_message(LwqqClient *lc, LwqqMsgMessage *mmsg)
{
    qq_account* ac = (qq_account*)lwqq_client_userdata(lc);
    const char* from = mmsg->super.from;
    static char buf[BUFLEN]={0};
    strcpy(buf,"");
    translate_struct_to_message(ac,mmsg,buf);
    if(contact(QString(from)))
        contact(QString(from))->receivedMessage(stransMsg(QString::fromUtf8(buf)));
}

void QQAccount::ac_qq_msg_check(LwqqClient *lc)
{
    printf("[%s] start\n", __FUNCTION__);
    if(!lwqq_client_valid(lc))
        return;
    LwqqRecvMsgList* l = lc->msg_list;
    LwqqRecvMsg *msg,*prev;
    LwqqMsgSystem* sys_msg;

    pthread_mutex_lock(&l->mutex);
    if (TAILQ_EMPTY(&l->head)) {
        /* No message now, wait 100ms */
        pthread_mutex_unlock(&l->mutex);
        return ;
    }
    msg = TAILQ_FIRST(&l->head);
    while(msg) {
        int res = SUCCESS;
        if(msg->msg) {
            switch(lwqq_mt_bits(msg->msg->type)) {
            case LWQQ_MT_MESSAGE:
                switch(msg->msg->type){
                case LWQQ_MS_BUDDY_MSG:
                    //printf("this is a message\n");
                    buddy_message(lc,(LwqqMsgMessage*)msg->msg);
                    break;
                case LWQQ_MS_GROUP_MSG:
                case LWQQ_MS_DISCU_MSG:
                case LWQQ_MS_GROUP_WEB_MSG:
                    group_message(lc,(LwqqMsgMessage*)msg->msg);
                    break;
                case LWQQ_MS_SESS_MSG:
                    whisper_message(lc,(LwqqMsgMessage*)msg->msg);
                    break;
                default:
                    break;
                }
                break;
            case LWQQ_MT_STATUS_CHANGE:
                kDebug(WEBQQ_GEN_DEBUG)<<"LWQQ_MT_STATUS_CHANGE";
                //status_change(lc,(LwqqMsgStatusChange*)msg->msg);
                break;
            case LWQQ_MT_KICK_MESSAGE:
                kDebug(WEBQQ_GEN_DEBUG)<<"LWQQ_MT_KICK_MESSAGE";
                //kick_message(lc,(LwqqMsgKickMessage*)msg->msg);
                break;
            case LWQQ_MT_SYSTEM:
#if 1
                sys_msg = (LwqqMsgSystem*)msg->msg;
                if(sys_msg->type == LwqqMsgSystem::VERIFY_REQUIRED){
                    msg->msg = NULL;
                    LwqqBuddy* buddy = lwqq_buddy_new();
                    LwqqAsyncEvent* ev = lwqq_info_get_stranger_info(
                                lc, sys_msg->super.from, buddy);
                    lwqq_async_add_event_listener(ev,
                                                  _C_(3p,system_message,lc,sys_msg,buddy));
                }else{
                    system_message(lc,(LwqqMsgSystem*)msg->msg,NULL);
                }
#endif 
                break;
            case LWQQ_MT_BLIST_CHANGE:
                kDebug(WEBQQ_GEN_DEBUG)<<"LWQQ_MT_BLIST_CHANGE";
                blist_change(lc,(LwqqMsgBlistChange*)msg->msg);
                break;
            case LWQQ_MT_OFFFILE:
                kDebug(WEBQQ_GEN_DEBUG)<<"LWQQ_MT_OFFFILE";
                //offline_file(lc,(LwqqMsgOffFile*)msg->msg);
                break;
            case LWQQ_MT_FILE_MSG:
                kDebug(WEBQQ_GEN_DEBUG)<<"LWQQ_MT_FILE_MSG";
                //file_message(lc,(LwqqMsgFileMessage*)msg->msg);
                break;
            case LWQQ_MT_FILETRANS:
                kDebug(WEBQQ_GEN_DEBUG)<<"LWQQ_MT_FILETRANS";
                //complete_file_trans(lc,(LwqqMsgFileTrans*)msg->msg->opaque);
                break;
            case LWQQ_MT_NOTIFY_OFFFILE:
                kDebug(WEBQQ_GEN_DEBUG)<<"LWQQ_MT_NOTIFY_OFFFILE";
                //notify_offfile(lc,(LwqqMsgNotifyOfffile*)msg->msg);
                break;
            case LWQQ_MT_INPUT_NOTIFY:
                kDebug(WEBQQ_GEN_DEBUG)<<"LWQQ_MT_INPUT_NOTIFY";
                //input_notify(lc,(LwqqMsgInputNotify*)msg->msg);
                break;
            case LWQQ_MT_SYS_G_MSG:
                kDebug(WEBQQ_GEN_DEBUG)<<"LWQQ_MT_SYS_G_MSG";
                //sys_g_message(lc,(LwqqMsgSysGMsg*)msg->msg);
                break;
            case LWQQ_MT_SHAKE_MESSAGE:
                kDebug(WEBQQ_GEN_DEBUG)<<"LWQQ_MT_SHAKE_MESSAGE";
                //shake_message(lc,(LwqqMsgShakeMessage*)msg->msg);
                break;
            default:
                //lwqq_puts("unknow message\n");
                break;
            }
        }

        prev = msg;
        msg = TAILQ_NEXT(msg,entries);
        if(res == SUCCESS){
            TAILQ_REMOVE(&l->head,prev, entries);
            lwqq_msg_free(prev->msg);
            s_free(prev);
        }
    }
    pthread_mutex_unlock(&l->mutex);
    return ;
}

void QQAccount::blist_change(LwqqClient *lc, LwqqMsgBlistChange *blist)
{
    LwqqSimpleBuddy* sb;
    LwqqBuddy* buddy;
    LwqqAsyncEvent* ev;
    LwqqAsyncEvset* set;
    LIST_FOREACH(sb,&blist->added_friends,entries){
        //in this. we didn't add it to fast cache.
        buddy = lwqq_buddy_find_buddy_by_uin(lc, sb->uin);
        if(!buddy->qqnumber){
            set = lwqq_async_evset_new();
            ev = lwqq_info_get_friend_qqnumber(lc,buddy);
            lwqq_async_evset_add_event(set, ev);
            ev = lwqq_info_get_friend_detail_info(lc, buddy);
            lwqq_async_evset_add_event(set, ev);
            lwqq_async_add_evset_listener(set,_C_(2p,write_buddy_to_db,lc,buddy));
        }else{
            friend_come(lc, buddy);
        }
    }
}

void QQAccount::ac_friend_avatar(LwqqClient* lc, LwqqBuddy *buddy)
{   
    QByteArray bytearry(buddy->avatar, buddy->avatar_len);
    //kDebug(WEBQQ_GEN_DEBUG)<<"buddy qqname:"<<QString::fromUtf8(buddy->qqnumber)<<"uin:"<<QString::fromUtf8(buddy->uin);
    /*find out the contact*/
    //kDebug(WEBQQ_GEN_DEBUG)<<"find out the contact";
    if(QString(buddy->qqnumber) == myself()->contactId())
    {
        Kopete::AvatarManager::AvatarEntry entry;
        entry.name = myself ()->contactId();
        entry.category = Kopete::AvatarManager::Contact;
        entry.contact = myself();
        entry.image = QImage::fromData(bytearry);
        entry = Kopete::AvatarManager::self()->add(entry);
        if (!entry.dataPath.isNull())
        {
            myself()->setProperty(Kopete::Global::Properties::self()->photo (), entry.dataPath);
        }
        myself()->setOnlineStatus( m_targetStatus );
    }else{
        QQContact * friendContact;
        if ( !contact(QString::fromUtf8(buddy->qqnumber)))
        {
            if(!contact(QString::fromUtf8(buddy->uin)))
                return;
            else
                friendContact = contact(QString::fromUtf8(buddy->uin));
        }else
            friendContact = contact(QString::fromUtf8(buddy->qqnumber));
        if(!friendContact)
            return;
        QString displayName;
        if (buddy->markname != NULL)
        {
            displayName = QString::fromUtf8(buddy->markname);
        }
        else
        {
            if (QString(buddy->nick).length() == 0)
                displayName = QString::fromUtf8(buddy->qqnumber);
            else
                displayName = QString::fromUtf8(buddy->nick);
        }
        /*first set its stat*/
        Kopete::OnlineStatus qqStatus = statusFromLwqqStatus(buddy->stat);
        friendContact->setOnlineStatus(qqStatus);
        friendContact->setContactType(Contact_Chat);
        friendContact->setProperty(Kopete::Global::Properties::self ()->nickName (), displayName);
        //friendContact->setNickName(displayName);
        QObject::connect(friendContact, SIGNAL(getUserInfoSignal(QString,ConType)), \
                         this, SLOT(slotGetUserInfo(QString,ConType)));
        /*set the contact's icon*/
        // kDebug(WEBQQ_GEN_DEBUG) <<"contact icon:" << bytearry.size();
        if (!bytearry.isEmpty())
        {
            friendContact->setDisplayPicture(bytearry);
        }else{

            //kDebug(WEBQQ_GEN_DEBUG)<<"avatarData size:" << avatarData.size();
            //QFile::remove(filename);
            //friendContact->setDisplayPicture(avatarData);
        }
    }
    buddy->avatar = NULL;
    buddy->avatar_len = 0;
}

void QQAccount::ac_group_avatar(LwqqClient *lc, LwqqGroup *group)
{
    QByteArray bytearry(group->avatar, group->avatar_len);

    /*find out the contact*/
    kDebug(WEBQQ_GEN_DEBUG)<<"find out the contact";
    QQContact * contact = dynamic_cast<QQContact *>(contacts().value( QString(group->gid) ));
    if ( !contact)
        return;

    contact->setContactType(Contact_Group);

    /*first set its stat*/
    //Kopete::OnlineStatus qqStatus = statusFromLwqqStatus(buddy->stat);
    //contact->setOnlineStatus(m_protocol->WebqqOnline );

    /*set the contact's icon*/
    //kDebug(WEBQQ_GEN_DEBUG) <<"contact icon:" << bytearry.size();
    if (!bytearry.isEmpty())
    {
        contact->setDisplayPicture(bytearry);
    }else{

        //        kDebug(WEBQQ_GEN_DEBUG)<<"avatarData size:" << avatarData.size();
        //        //QFile::remove(filename);
        //        contact->setDisplayPicture(avatarData);
    }
}

void QQAccount::ac_group_members(LwqqClient *lc, LwqqGroup *group)
{
    LwqqSimpleBuddy* member;
    LwqqBuddy* buddy;
    QString g_id = QString((group->type == LWQQ_GROUP_QUN? group->gid:group->did));
    if(!contact(g_id))
        return;
    contact(g_id)->set_group_status(true);
    contact(g_id)->setContactType(Contact_Group);
    if(group->type == LWQQ_GROUP_DISCU || group->type == LWQQ_GROUP_QUN)
    {
        LIST_FOREACH(member,&group->members,entries)
        {
            QString contactName;
            if(member->card)
                contactName = QString::fromUtf8(member->card);
            else if(member->nick)
                contactName = QString::fromUtf8(member->nick);
            else
                contactName = QString::fromUtf8(member->uin);
            if(strcmp(lc->myself->uin, member->uin) != 0)
            {
                if((buddy = find_buddy_by_uin(lc,member->uin))) {
                    if(contact(QString(buddy->qqnumber)))
                    {
                        contact(g_id)->qq_addcontacts(contact(QString(buddy->qqnumber)));
                    }else if(contact(QString(buddy->uin)))
                    {
                        contact(g_id)->qq_addcontacts(contact(QString(buddy->uin)));
                    }else
                    {
                        if(buddy->qqnumber)
                        {
                            if( createChatSessionContact( QString(buddy->qqnumber)  , contactName))
                            {
                                if(!contact(QString(buddy->qqnumber)))
                                    return;
                                contact(QString(buddy->qqnumber))->setContactType(Contact_Session);
                                contact(QString(buddy->qqnumber))->set_session_info(QString((group->type == LWQQ_GROUP_QUN? group->gid:group->did)), \
                                                                                    QString::fromUtf8(group->name));
                                contact(QString(buddy->qqnumber))->setProperty(Kopete::Global::Properties::self ()->nickName (), contactName);
                                //contact(QString(buddy->qqnumber))->setNickName(contactName);
                                //contact(QString(buddy->qqnumber))->setOnlineStatus(statusFromLwqqStatus(buddy->stat));
                                QObject::connect(contact(QString(buddy->qqnumber)), SIGNAL(getUserInfoSignal(QString,ConType)),\
                                                 this, SLOT(slotGetUserInfo(QString,ConType)));
                                contact(g_id)->qq_addcontacts(contact(QString(buddy->qqnumber)));
                            }
                        }else
                        {
                            if( createChatSessionContact( QString(buddy->uin)  , contactName))
                            {
                                if(!contact(QString(buddy->uin)))
                                    return;
                                contact(QString(buddy->uin))->setContactType(Contact_Session);
                                contact(QString(buddy->uin))->set_session_info(g_id, \
                                                                               QString::fromUtf8(group->name));
                                contact(QString(buddy->uin))->setProperty(Kopete::Global::Properties::self ()->nickName (), contactName);
                                //contact(QString(buddy->uin))->setNickName(contactName);
                                //contact(QString(buddy->uin))->setOnlineStatus(statusFromLwqqStatus(buddy->stat));
                                QObject::connect(contact(QString(buddy->uin)), SIGNAL(getUserInfoSignal(QString,ConType)),\
                                                 this, SLOT(slotGetUserInfo(QString,ConType)));
                                contact(g_id)->qq_addcontacts(contact(QString(buddy->uin)));
                            }
                        }
                    }
                }else{
                    LwqqBuddy* buddy = lwqq_buddy_new();
                    buddy->qqnumber = member->qq;
                    buddy->nick = member->nick;
                    buddy->uin = member->uin;
                    if(member->qq)
                    {
                        if(createChatSessionContact( QString(member->qq)  ,contactName ))
                        {
                            if(!contact(QString(member->qq)))
                                return;
                            contact(QString(member->qq) )->setContactType(Contact_Session);
                            contact(QString(member->qq) )->set_session_info(g_id, QString::fromUtf8(group->name));
                            contact(QString(member->qq))->setProperty(Kopete::Global::Properties::self ()->nickName (), contactName);
                            //contact(QString(buddy->qq))->setNickName(contactName);
                            //contact(QString(member->qq))->setOnlineStatus(statusFromLwqqStatus(member->stat));
                            QObject::connect(contact(QString(member->qq)), SIGNAL(getUserInfoSignal(QString,ConType)),\
                                             this, SLOT(slotGetUserInfo(QString,ConType)));
                            contact(g_id)->qq_addcontacts(contact(QString(member->qq)));
                        }
                    }else if(member->uin)
                    {
                        if( createChatSessionContact( QString(member->uin)  ,contactName ))
                        {
                            if(!contact(QString(member->uin) ))
                                return;
                            contact(QString(member->uin) )->setContactType(Contact_Session);
                            contact(QString(member->uin) )->set_session_info(g_id, QString::fromUtf8(group->name));
                            contact(QString(member->uin))->setProperty(Kopete::Global::Properties::self ()->nickName (), contactName);
                            //contact(QString(buddy->uin))->setNickName(contactName);
                            //contact(QString(member->uin))->setOnlineStatus(statusFromLwqqStatus(member->stat));
                            QObject::connect(contact(QString(member->uin)), SIGNAL(getUserInfoSignal(QString,ConType)),\
                                             this, SLOT(slotGetUserInfo(QString,ConType)));
                            contact(g_id)->qq_addcontacts(contact(QString(member->uin)));
                        }
                    }
                    LIST_INSERT_HEAD(&lc->friends,buddy,entries);
                    lwdb_userdb_insert_buddy_info(((qq_account*)(lc->data))->db, &buddy);
                }
            }
        }
        QQContact *gContact = contact(g_id);
        if(gContact)
        {
            gContact->set_group_members();
            //gContact->setOnlineStatus( WebqqProtocol::protocol()->WebqqOnline);
        }
    }
    contact(g_id)->set_group_status(false);

#if 0
    LIST_FOREACH(member,&group->members,entries)
    {
        QString contactName;
        if(member->card)
            contactName = QString::fromUtf8(member->card);
        else
            contactName = QString::fromUtf8(member->nick);
        //        kDebug(WEBQQ_GEN_DEBUG)<<"groupname:"<<contactName<<"qq:"<<QString(member->uin)<<QString(group->gid);
        //        kDebug(WEBQQ_GEN_DEBUG)<<"clientid:"<<QString(lc->clientid)<<"uin"<<QString(lc->myself->uin);
        if(strcmp(lc->myself->uin, member->uin) != 0)
        {
            if((buddy = find_buddy_by_uin(lc,member->uin))) {
                if( !contact(QString(buddy->qqnumber)))
                {
                    LwqqAsyncEvset* set = lwqq_async_evset_new();
                    LwqqAsyncEvent* ev;
                    ev = lwqq_info_get_friend_avatar(m_lc,buddy);
                    lwqq_async_evset_add_event(set,ev);
                    ev = lwqq_info_get_friend_detail_info(m_lc, buddy);/*get detail of friend*/
                    lwqq_async_evset_add_event(set,ev);
                    lwqq_async_add_evset_listener(set,_C_(2p,cb_friend_avatar, m_lc, buddy));
                    addContact( QString(buddy->qqnumber), contactName,  targetGroup, Kopete::Account::ChangeKABC);
                    QQContact * addedContact = dynamic_cast<QQContact *>(contacts().value( QString(buddy->qqnumber) ));
                    addedContact->setContactType(Contact_Chat);
                    LIST_INSERT_HEAD(&m_lc->friends,buddy,entries);
                    lwdb_userdb_insert_buddy_info(((qq_account*)(m_lc->data))->db, buddy);

                }
                contact(QString((group->type == LWQQ_GROUP_QUN? group->gid:group->did)))->qq_addcontacts(contact(QString(buddy->qqnumber)));
            }else{

                if( !contact(QString(member->uin)))
                {
                    LwqqBuddy* buddy = lwqq_buddy_new();
                    buddy->qqnumber = member->qq;
                    buddy->nick = member->nick;
                    buddy->uin = member->uin;
                    LwqqAsyncEvset* set = lwqq_async_evset_new();
                    LwqqAsyncEvent* ev;
                    ev = lwqq_info_get_friend_avatar(m_lc,buddy);
                    lwqq_async_evset_add_event(set,ev);
                    ev = lwqq_info_get_friend_detail_info(m_lc, buddy);/*get detail of friend*/
                    lwqq_async_evset_add_event(set,ev);
                    lwqq_async_add_evset_listener(set,_C_(2p,cb_friend_avatar, m_lc, buddy));
                    addContact( QString(member->uin), contactName,  targetGroup, Kopete::Account::ChangeKABC);
                    QQContact * addedContact = dynamic_cast<QQContact *>(contacts().value( QString(buddy->uin) ));
                    addedContact->setContactType(Contact_Session);
                    addedContact->set_session_info(QString((group->type == LwqqGroup::LWQQ_GROUP_QUN? group->gid:group->did)), QString::fromUtf8(group->name));
                    LIST_INSERT_HEAD(&m_lc->friends,buddy,entries);
                    lwdb_userdb_insert_buddy_info(((qq_account*)(m_lc->data))->db, buddy);

                }

                contact(QString((group->type == LwqqGroup::LWQQ_GROUP_QUN? group->gid:group->did)))->qq_addcontacts(contact(QString(member->uin)));
            }
        }
    }
    contact(QString((group->type == LwqqGroup::LWQQ_GROUP_QUN? group->gid:group->did)))->set_group_members();
#endif
}

void QQAccount::ac_need_verify2(LwqqClient* lc, LwqqVerifyCode* code)
{
    //printf("[%s] \n", __FUNCTION__);
    const char *dir = "/tmp/kopete-qq/";
    // char fname[50];
    //snprintf(fname,sizeof(fname),"%s.gif",lc->username);
    lwqq_util_save_img(code->data,code->size,lc->username,dir);
    LoginVerifyDialog *dlg = new LoginVerifyDialog();
    QString fullPath = QString(dir) + QString(lc->username);
    dlg->setImage(fullPath);
    dlg->exec();
    QString input = dlg->getVerifyString();
    if(input.isEmpty())
    {
        KMessageBox::information(Kopete::UI::Global::mainWidget(), i18n("Please input verify"), i18n("verify"));
        dlg->exec();
        input = dlg->getVerifyString();
    }
    code->str = s_strdup(input.toAscii().data());
    vp_do(code->cmd,NULL);
    
    delete dlg;
    
    return ;
}




void QQAccount::ac_login_stage_1(LwqqClient* lc,LwqqErrorCode* p_err)
{
    LwqqErrorCode err = *p_err;
    QString message;
    if(!lwqq_client_valid(lc)) return;
    switch(err){
    case LWQQ_EC_OK:
        message = i18n( "Connect OK");
        //KNotification::event( "Connect OK", message, myself()->onlineStatus().protocolIcon(KIconLoader::SizeMedium) );
        //myself()->setOnlineStatus( m_targetStatus );
        password().setWrong(false);

        //afterLogin(lc);
        break;
    case LWQQ_EC_LOGIN_ABNORMAL:
        disconnected( Manual );			// don't reconnect
        message = i18n( "Login Abnormal!");
        KMessageBox::queuedMessageBox(Kopete::UI::Global::mainWidget(), KMessageBox::Error, message);
        myself()->setOnlineStatus( m_protocol->WebqqOffline );
        return ;
    case LWQQ_EC_NETWORK_ERROR:
        disconnected( Manual );			// don't reconnect
        myself()->setOnlineStatus( m_protocol->WebqqOffline );
        message = i18n( "Network error!");
        KMessageBox::queuedMessageBox(Kopete::UI::Global::mainWidget(), KMessageBox::Error, message);
        return;
    case LWQQ_EC_LOGIN_NEED_BARCODE:
        kDebug(WEBQQ_GEN_DEBUG)<<"error:"<<err;
        disconnected( Manual );			// don't reconnect
        myself()->setOnlineStatus( m_protocol->WebqqOffline );
        message = i18n(lc->error_description);
        KMessageBox::queuedMessageBox(Kopete::UI::Global::mainWidget(), KMessageBox::Error, message);
        return;
    default:
        kDebug(WEBQQ_GEN_DEBUG)<<"error:"<<err;
        disconnected( Manual );			// don't reconnect
        myself()->setOnlineStatus( m_protocol->WebqqOffline );
        message = i18n(lc->last_err);
        KMessageBox::queuedMessageBox(Kopete::UI::Global::mainWidget(), KMessageBox::Error, message);
        return;
    }

    //    ac->state = CONNECTED;
    LwqqAsyncEvent* ev = lwqq_info_get_friends_info(lc, NULL, NULL);
    lwqq_async_add_event_listener(ev, _C_(p,friends_valid_hash,ev));
    return ;
}

void QQAccount::pollMessage()
{
    if(!m_lc)
        return;

    LwqqRecvMsgList *msgList = lwqq_msglist_new(m_lc);

    lwqq_msglist_poll(msgList, POLL_AUTO_DOWN_BUDDY_PIC);
    //lwqq_msglist_free(msgList);

}

void QQAccount::ac_login_stage_2(LwqqAsyncEvent* event,LwqqClient* lc)
{
    if(event->result != int(LWQQ_EC_OK)){
        KMessageBox::queuedMessageBox(Kopete::UI::Global::mainWidget(), KMessageBox::Error, \
                                      i18n("Get Group List Failed"));
        //printf("Get group list failed, error code is %d\n", event->result);
        return;
    }
    LwqqAsyncEvset* set = lwqq_async_evset_new();
    LwqqAsyncEvent* ev;
    ev = lwqq_info_get_discu_name_list(lc);
    lwqq_async_evset_add_event(set,ev);
    ev = lwqq_info_get_online_buddies(lc,NULL);
    lwqq_async_evset_add_event(set,ev);
    

    ev = lwqq_info_get_friend_detail_info(lc,lc->myself);
    lwqq_async_evset_add_event(set,ev);
    lwqq_async_add_evset_listener(set,_C_(p,cb_login_stage_3,lc));

}

void QQAccount::friend_come(LwqqClient* lc,LwqqBuddy* buddy)
{
    LwqqFriendCategory* cate;
    int cate_index = buddy->cate_index;
    QString categoryName;

    if(cate_index == LWQQ_FRIEND_CATE_IDX_PASSERBY)
    {
        categoryName = QString("Passerby");
    }
    else
    {
        LIST_FOREACH(cate,&lc->categories,entries)
        {
            if(cate->index==cate_index)
            {
                categoryName = QString::fromUtf8(cate->name);
                break;
            }
        }
    }
    
    QList<Kopete::Group*> groupList = Kopete::ContactList::self()->groups();
    Kopete::Group *g;
    Kopete::Group *targetGroup = NULL;
    
    foreach(g, groupList)
    {
        if(g->displayName() == categoryName)
        {
            targetGroup = g;
            break;
        }
    }
    /*if not found this group, add a new group*/
    if (targetGroup == NULL)
    {
        targetGroup = new Kopete::Group(categoryName);
        Kopete::ContactList::self()->addGroup( targetGroup );
    }
    
    /*get buddy's display name*/
    QString displayName;
    if (buddy->markname != NULL)
    {
        displayName = QString::fromUtf8(buddy->markname);
    }
    else
    {
        if (QString(buddy->nick).length() == 0)
            displayName = QString::fromUtf8(buddy->qqnumber);
        else
            displayName = QString::fromUtf8(buddy->nick);
    }
    
    //this is avaliable when reload avatar in
    //login_stage_f
    if(buddy->avatar_len)
    {
        ac_friend_avatar(lc, buddy);
    }
    else
    {
        LwqqAsyncEvent* ev = lwqq_info_get_friend_avatar(lc,buddy);
        lwqq_async_add_event_listener(ev,_C_(2p,cb_friend_avatar,lc,buddy));
    }
    //    if(contact(QString(buddy->qqnumber)))
    //        fprintf(stderr, "contact %s\n", contact(QString(buddy->qqnumber))->property(Kopete::Global::Properties::self()->photo()).tmpl().icon().toUtf8().data());

    //    if(Kopete::AvatarManager::self()->exists(QString(buddy->qqnumber)))
    //        fprintf(stderr, "AvatarManager exists\n");
    addContact( QString(buddy->qqnumber), displayName,  targetGroup, Kopete::Account::ChangeKABC );

}

void QQAccount::initCategory()
{
    printf("Try to init Category\n");
    LwqqFriendCategory* cate;
    

    QList<Kopete::Group*> groupList = Kopete::ContactList::self()->groups();
    Kopete::Group *g;
    Kopete::Group *newGroup;
    
    foreach(g, groupList)
    {
        Kopete::ContactList::self()->removeGroup( g );
    }

    /*add new group*/
    LIST_FOREACH(cate,&m_lc->categories,entries) {
        QString groupName = QString::fromUtf8(cate->name);;
        newGroup = new Kopete::Group(groupName);
        Kopete::ContactList::self()->addGroup( newGroup );
    }


}

QQContact *QQAccount::contact(const QString &id)
{
    return dynamic_cast<QQContact *>(contacts().value( id ));
}

void QQAccount::group_come(LwqqClient* lc,LwqqGroup* group)
{
    QString categoryName;
    MsgDataList msgList;
    msgList.clear();
    if(group->type == LWQQ_GROUP_QUN)
    {
        categoryName = i18n("Group");
        m_msgMap.insert(QString(group->gid), msgList);
    }
    else  if(group->type == LWQQ_GROUP_DISCU)
    {
        categoryName = i18n("Discussion");
        m_msgMap.insert(QString(group->did), msgList);
    }

    QList<Kopete::Group*> groupList = Kopete::ContactList::self()->groups();
    Kopete::Group *g;
    Kopete::Group *targetGroup = NULL;
    foreach(g, groupList)
    {
        if(g->displayName() == categoryName)
        {
            targetGroup = g;
            break;
        }
    }
    /*if not found this group, add a new group*/
    if (targetGroup == NULL)
    {
        targetGroup = new Kopete::Group(categoryName);
        Kopete::ContactList::self()->addGroup( targetGroup );
    }

    /*get buddy's display name*/
    QString displayName;
    if (group->markname != NULL)
    {
        displayName = QString::fromUtf8(group->markname);
    }
    else
    {
        if (QString(group->name).length() == 0)
            displayName = QString::fromUtf8(group->gid);
        else
            displayName = QString::fromUtf8(group->name);
    }

    //this is avaliable when reload avatar in
    //login_stage_f
    if(group->avatar_len)
    {
        ac_group_avatar(lc, group);
    }
    else
    {
        LwqqAsyncEvent* ev = lwqq_info_get_group_avatar(lc,group);
        lwqq_async_add_event_listener(ev,_C_(2p,cb_group_avatar,lc,group));

    }
    if(group->type == LWQQ_GROUP_QUN)
    {
        addContact( QString(group->gid), displayName,  targetGroup, Kopete::Account::ChangeKABC );
        if(!contact(QString(group->gid)))
            return;
        QObject::connect(contact(QString(group->gid)) , SIGNAL(getGroupMembersSignal(QString)), \
                         this, SLOT(slotGetGroupMembers(QString)));
        QObject::connect(contact(QString(group->gid)) , SIGNAL(blockSignal(QString)), this, SLOT(slotBlock(QString)));
        QObject::connect(contact(QString(group->gid)), SIGNAL(getUserInfoSignal(QString,ConType)),\
                         this, SLOT(slotGetUserInfo(QString,ConType)));
    }else if(group->type == LWQQ_GROUP_DISCU)
    {
        addContact( QString(group->did), displayName,  targetGroup, Kopete::Account::ChangeKABC );
        if(!contact(QString(group->did)))
            return;
        QObject::connect(contact(QString(group->did)) , SIGNAL(getGroupMembersSignal(QString)), \
                         this, SLOT(slotGetGroupMembers(QString)));
        QObject::connect(contact(QString(group->did)) , SIGNAL(blockSignal(QString)), this, SLOT(slotBlock(QString)));
        QObject::connect(contact(QString(group->did)), SIGNAL(getUserInfoSignal(QString,ConType)),\
                         this, SLOT(slotGetUserInfo(QString,ConType)));
    }

    ac_qq_set_group_name(group);
}

void QQAccount::slotGetGroupMembers(QString id)
{
    LwqqGroup* group = find_group_by_gid(m_lc,id.toUtf8().constData());
    if(group == NULL)
        return;
    if(LIST_EMPTY(&group->members))
    {
        LwqqAsyncEvent* ev = lwqq_info_get_group_detail_info(m_lc, group, NULL);;
        lwqq_async_add_event_listener(ev,_C_(2p,cb_group_members,m_lc,group));
    }
    if(m_msgMap[id].size() > 0)
    {
        rewrite_group_msg(id);
    }
}

void QQAccount::discu_come(LwqqClient *lc, LwqqGroup *group)
{
    group_come(lc, group);
}
void QQAccount::ac_login_stage_3(LwqqClient* lc)
{
    kDebug(WEBQQ_GEN_DEBUG)<<"ac_login_stage_3";
    qq_account* ac = (qq_account*)lwqq_client_userdata(lc);



    lwdb_userdb_flush_buddies(ac->db, 5,5);
    lwdb_userdb_flush_groups(ac->db, 1,10);

    if(ac->flag & QQ_USE_QQNUM){  //QQ_USE_QQNUM
        lwdb_userdb_query_qqnumbers(ac->db,lc);
    }
//    myself ()->setProperty (Kopete::Global::Properties::self ()->nickName (),
//                            QString::fromUtf8(lc->myself->nick));
    myself()->setNickName(QString::fromUtf8(lc->myself->nick));
    //kDebug(WEBQQ_GEN_DEBUG)<<"photo path:"<<myself()->property(Kopete::Global::Properties::self()->photo()).value().toString();
    LwqqAsyncEvent* lwev=lwqq_info_get_friend_avatar(lc,lc->myself);
    lwqq_async_add_event_listener(lwev,_C_(2p,cb_friend_avatar,ac,lc->myself));

    LwqqAsyncEvent* ev = NULL;
    LwqqAsyncEvset* set = lwqq_async_evset_new();
    LwqqBuddy* buddy;
    LIST_FOREACH(buddy,&lc->friends,entries) {
        lwdb_userdb_query_buddy(ac->db, buddy);
        if((ac->flag& QQ_USE_QQNUM)&&! buddy->qqnumber){
            ev = lwqq_info_get_friend_qqnumber(lc,buddy);
            lwqq_async_evset_add_event(set, ev);
        }
        if(buddy->last_modify == 0 || buddy->last_modify == -1) {
            ev = lwqq_info_get_single_long_nick(lc, buddy);
            lwqq_async_evset_add_event(set, ev);
            ev = lwqq_info_get_level(lc, buddy);
            lwqq_async_evset_add_event(set, ev);
            //if buddy is unknow we should update avatar in friend_come
            //for better speed in first load
            if(buddy->last_modify == LWQQ_LAST_MODIFY_RESET){
                ev = lwqq_info_get_friend_avatar(lc,buddy);
                lwqq_async_evset_add_event(set, ev);
            }
        }
        if(buddy->last_modify != -1 && buddy->last_modify != 0)
            friend_come(lc,buddy);
    }
    
    LwqqGroup* group;
    LIST_FOREACH(group,&lc->groups,entries) {
        //LwqqAsyncEvset* set = NULL;
        lwdb_userdb_query_group(ac->db, group);
        if((ac->flag && 1)&& ! group->account){ //QQ_USE_QQNUM = 1

            ev = lwqq_info_get_group_qqnumber(lc,group);
            lwqq_async_evset_add_event(set, ev);
        }
        if(group->last_modify == -1 || group->last_modify == 0){
            ev = lwqq_info_get_group_memo(lc, group);
            lwqq_async_evset_add_event(set, ev);
        }
        //because group avatar less changed.
        //so we dont reload it.

        if(group->last_modify != -1 && group->last_modify != 0)
            group_come(lc,group);
    }

    LwqqGroup* discu;
    LIST_FOREACH(discu,&lc->discus,entries){
        if(ac->flag&CACHE_TALKGROUP && discu->last_modify == LWQQ_LAST_MODIFY_UNKNOW)
            // discu is imediately date, doesn't need get info from server, we can
            // directly write it into database
            lwdb_userdb_insert_discu_info(ac->db, &discu);
        discu_come(lc, discu);
    }


    lwqq_async_add_evset_listener(set, _C_(p,cb_login_stage_f,lc));

    ac->state = LOAD_COMPLETED;

}


void QQAccount::ac_login_stage_f(LwqqClient* lc)
{
    kDebug(WEBQQ_GEN_DEBUG)<<"ac_login_stage_f";
    LwqqBuddy* buddy;
    LwqqGroup* group,*discu;
    qq_account* ac = (qq_account*)lc->data;
    lwdb_userdb_begin(ac->db);
    LIST_FOREACH(buddy,&lc->friends,entries) {
        if(buddy->last_modify == -1 || buddy->last_modify == 0){
            lwdb_userdb_insert_buddy_info(ac->db, &buddy);
            friend_come(lc, buddy);
        }
    }
    LIST_FOREACH(group,&lc->groups,entries){
        if(group->last_modify == -1 || group->last_modify == 0){
            lwdb_userdb_insert_group_info(ac->db, &group);
            group_come(lc,group);
        }
    }
    lwdb_userdb_commit(ac->db);
    myself()->setOnlineStatus( m_targetStatus );

    LwqqPollOption flags = POLL_AUTO_DOWN_DISCU_PIC|POLL_AUTO_DOWN_GROUP_PIC|POLL_AUTO_DOWN_BUDDY_PIC;
    if(ac->flag& REMOVE_DUPLICATED_MSG)
        flags |= POLL_REMOVE_DUPLICATED_MSG;
    if(ac->flag& NOT_DOWNLOAD_GROUP_PIC)
        flags &= ~POLL_AUTO_DOWN_GROUP_PIC;

    lwqq_msglist_poll(lc->msg_list, flags);
}




void QQAccount::setStatusMessage(const Kopete::StatusMessage& statusMessage)
{
    Q_UNUSED(statusMessage);
    /* Not used in qq */
}

#if 0
void QQAccount::connect( const Kopete::OnlineStatus& /* initialStatus */ )
{
    kDebug ( 14210 ) ;
    //myself()->setOnlineStatus( WebqqProtocol::protocol()->qqOnline );
    login();
    QObject::connect ( m_server, SIGNAL (messageReceived(QString)),
                       this, SLOT (receivedMessage(QString)) );
}
#endif

void QQAccount::connectWithPassword( const QString &passwd )
{
    fprintf(stderr, "connect password\n");
    if ( isAway() )
    {
        slotGoOnline();
        return;
    }

    if ( isConnected() ||
         myself()->onlineStatus() == m_protocol->WebqqConnecting )
    {
        return;

    }

    /*
     * Fixed Bug: first login after create account
     *
     */
    if (m_targetStatus == Kopete::OnlineStatus::Offline)
    {
        m_targetStatus = Kopete::OnlineStatus::Online;
        slotGoOnline();
        return;
    }

    if ( passwd.isNull() )
    { //cancel the connection attempt
        static_cast<QQContact*>( myself() )->setOnlineStatus( m_protocol->WebqqOffline );
        return;
    }


    static_cast<QQContact *>( myself() )->setOnlineStatus( m_protocol->WebqqConnecting );


    /*
     * !Urgly,
     *change lwqqclient username and password
    */
    if(m_lc == NULL)
        initLwqqAccount();

    s_free(m_lc->username);
    s_free(m_lc->password);
    m_lc->username = s_strdup(accountId().toAscii().constData());
    m_lc->password = s_strdup(passwd.toAscii().constData());

    /*first get the connect type*/
    LwqqStatus targetLwqqStatus;
    if (m_targetStatus == Kopete::OnlineStatus::Online)
    {
        targetLwqqStatus = LWQQ_STATUS_ONLINE;
    }
    else if(m_targetStatus == Kopete::OnlineStatus::Away)
    {
        targetLwqqStatus = LWQQ_STATUS_AWAY;
    }
    else if(m_targetStatus == Kopete::OnlineStatus::Busy)
        targetLwqqStatus = LWQQ_STATUS_BUSY;
    else if(m_targetStatus == Kopete::OnlineStatus::Invisible)
        targetLwqqStatus = LWQQ_STATUS_HIDDEN;
    else
    {
        targetLwqqStatus = LWQQ_STATUS_ONLINE;
    }

    /*try to connect to lwqq*/

    lwqq_login(m_lc, targetLwqqStatus, NULL);

}

bool QQAccount::isOffline()
{
    if(m_targetStatus == Kopete::OnlineStatus::Offline)
        return true;
    else
        return false;
}

void QQAccount::disconnect()
{
    kDebug ( 14210 ) ;
    destoryLwqqAccount();/*destory this lwqq client*/

    myself()->setOnlineStatus( m_protocol->WebqqOffline );

}


void QQAccount::slotGoOnline ()
{
    kDebug ( 14210 ) ;
    if (!isConnected ())
    {
        connect ();
    }
    else
    {
        myself()->setOnlineStatus( m_protocol->WebqqOnline );
        lwqq_info_change_status(m_lc, LWQQ_STATUS_ONLINE);
    }
    //updateContactStatus();
}

void QQAccount::slotGoAway ()
{
    kDebug ( 14210 ) ;


    if (!isConnected ())
        connect();
    else
    {
        myself()->setOnlineStatus( m_protocol->WebqqAway );
        lwqq_info_change_status(m_lc, LWQQ_STATUS_AWAY);
    }
    //updateContactStatus();
}

void QQAccount::slotGoHidden()
{
    kDebug ( 14210 ) ;

    if (!isConnected ())
        connect();
    else
    {
        myself()->setOnlineStatus( m_protocol->WebqqHidden );
        lwqq_info_change_status(m_lc, LWQQ_STATUS_HIDDEN);
    }
}


void QQAccount::slotGoBusy ()
{
    kDebug ( 14210 ) ;

    if (!isConnected ())
        connect();
    else
    {
        myself()->setOnlineStatus( m_protocol->WebqqBusy );
        lwqq_info_change_status(m_lc, LWQQ_STATUS_BUSY);
    }
    //updateContactStatus();
}


void QQAccount::slotGoOffline ()
{
    kDebug ( 14210 ) ;

    if (isConnected ())
        disconnect ();
    else
        updateContactStatus();
}

void QQAccount::slotShowVideo ()
{
    kDebug ( 14210 ) ;

    if (isConnected ())
    {
        //QQWebcamDialog *qqWebcamDialog = new QQWebcamDialog(0, 0);
        //Q_UNUSED(qqWebcamDialog);
    }
    //updateContactStatus();
}

void QQAccount::receivedMessage( const QString &message )
{
    // Look up the contact the message is from
    QString from;
    QQContact* messageSender;

    from = message.section( ':', 0, 0 );
    Kopete::Contact* contact = contacts().value(from);
    messageSender = dynamic_cast<QQContact *>( contact );

    //kWarning( 14210 ) << " got a message from " << from << ", " << messageSender << ", is: " << message;
    // Pass it on to the contact to pocess and display via a KMM
    if ( messageSender )
        messageSender->receivedMessage( message );
    else
        kWarning(14210) << "unable to look up contact for delivery";
}

void QQAccount::updateContactStatus()
{
    QHashIterator<QString, Kopete::Contact*>itr( contacts() );
    for ( ; itr.hasNext(); ) {
        itr.next();
        itr.value()->setOnlineStatus( myself()->onlineStatus() );
    }
}

void QQAccount::find_add_contact(const QString name, Find_Type type, QString groupName)
{
    m_groupName = groupName;
    m_contactName = name;
    if(type == Buddy)
    {
        kDebug(WEBQQ_GEN_DEBUG)<<"add buddy";
        qq_add_buddy(m_lc, name.toUtf8().constData(), NULL);
    }else if(type == Group)
    {

    }
}
//clean all contacts and groups
void QQAccount::cleanAll_contacts()
{
    QList<Kopete::Group*> groupList = Kopete::ContactList::self()->groups();
    QList<Kopete::MetaContact *> metaContactList =  Kopete::ContactList::self()->metaContacts();
    foreach ( Kopete::MetaContact* metaContact, metaContactList )
        Kopete::ContactList::self()->removeMetaContact( metaContact );

    foreach ( Kopete::Group* group, groupList )
        Kopete::ContactList::self()->removeGroup( group );

}

void QQAccount::ac_show_confirm_table(LwqqClient *lc, LwqqConfirmTable *table, add_info *info)
{
    kDebug(WEBQQ_GEN_DEBUG)<<"ac_show_confirm_table";
    m_addInfo = info;
    bool isExans = false;
    if(table->exans_label){
        ShowGetInfoDialog *dlg = new ShowGetInfoDialog();
        dlg->setRequired(QString::fromUtf8(table->body));
        dlg->exec();
        QString input = dlg->getVerificationString();
        if(dlg->okOrCancle() == "OK")
            confirm_table_yes(table, input.toUtf8().constData(), dlg->qqAnswer());
        else
            confirm_table_no(table, input.toUtf8().constData());
        delete dlg;
        isExans = true;
    }
    if(table->body && (!isExans)){
        ShowGetInfoDialog *dlg = new ShowGetInfoDialog();
        dlg->setAddInfo(QString::fromUtf8(table->body));
        dlg->exec();
        if(dlg->okOrCancle() == "OK")
            confirm_table_yes(table, NULL, 0);
        else
            confirm_table_no(table, NULL);
        delete dlg;

    }
    if(table->input_label){
        if(strcmp(table->input_label, "Longnick") == 0)
        {
            ShowGetInfoDialog *dlg = new ShowGetInfoDialog();
            dlg->setLongNick(QString::fromUtf8(table->input));
            dlg->exec();
            QString input = dlg->getVerificationString();
            if(dlg->okOrCancle() == "OK")
                confirm_table_yes(table, input.toUtf8().constData(), 0);
            else
                confirm_table_no(table, input.toUtf8().constData());
            delete dlg;
        }else{
            ShowGetInfoDialog *dlg = new ShowGetInfoDialog();
            dlg->setVerifify();
            dlg->exec();
            QString input = dlg->getVerificationString();
            if(dlg->okOrCancle() == "OK")
                confirm_table_yes(table, input.toUtf8().constData(), 0);
            else
                confirm_table_no(table, input.toUtf8().constData());
            delete dlg;
        }
    }

}

void QQAccount::ac_show_messageBox(msg_type type, const char *title, const char *message)
{
    if(type == MSG_INFO)
    {
        KMessageBox::information(Kopete::UI::Global::mainWidget(), i18n(message), i18n(title));
    }else if(type == MSG_ERROR)
    {
        KMessageBox::error(Kopete::UI::Global::mainWidget(), i18n(message), i18n(title));
    }else if(type == MSG_ADD)
    {
        KMessageBox::information(Kopete::UI::Global::mainWidget(), i18n(message), i18n(title));
        kDebug(WEBQQ_GEN_DEBUG)<<"add buddy!!!!";
        QList<Kopete::Group*> groupList = Kopete::ContactList::self()->groups();
        Kopete::Group *g;
        Kopete::Group *targetGroup = NULL;

        foreach(g, groupList)
        {
            if(g->displayName() == m_groupName)
            {
                targetGroup = g;
                break;
            }
        }
        kDebug(WEBQQ_GEN_DEBUG)<<"start add";
        LwqqBuddy* buddy = lwqq_buddy_new();
        buddy->qqnumber = m_addInfo->qq;
        buddy->nick = m_addInfo->name;
        buddy->uin = m_addInfo->uin;
        //kDebug(WEBQQ_GEN_DEBUG)<<"qq:"<<QString::fromUtf8(m_addInfo->qq)<<"name:"<<QString::fromUtf8(m_addInfo->name)<<"uin"<<QString::fromUtf8(m_addInfo->uin);
        LwqqAsyncEvset* set = lwqq_async_evset_new();
        LwqqAsyncEvent* ev;
        ev = lwqq_info_get_friend_avatar(m_lc,buddy);
        lwqq_async_evset_add_event(set,ev);
        ev = lwqq_info_get_friend_detail_info(m_lc, buddy);/*get detail of friend*/
        lwqq_async_evset_add_event(set,ev);
        lwqq_async_add_evset_listener(set,_C_(2p,cb_friend_avatar, m_lc, buddy));
        kDebug(WEBQQ_GEN_DEBUG)<<"lwqq_async_add_evset_listener";
        addContact(QString::fromUtf8(m_addInfo->qq), QString::fromUtf8(m_addInfo->name), targetGroup, Kopete::Account::ChangeKABC);
        kDebug(WEBQQ_GEN_DEBUG)<<"addContact";
        QQContact * addedContact = dynamic_cast<QQContact *>(contacts().value( QString(buddy->qqnumber) ));
        if(!addedContact)
            return;
        addedContact->setContactType(Contact_Chat);
        LIST_INSERT_HEAD(&m_lc->friends,buddy,entries);
        lwdb_userdb_insert_buddy_info(((qq_account*)(m_lc->data))->db, &buddy);
        kDebug(WEBQQ_GEN_DEBUG)<<"edn";
    }
}

void QQAccount::ac_friend_come(LwqqClient *lc, LwqqBuddy *b)
{
    friend_come(lc, b);
}

void QQAccount::slotBlock(QString id)
{
    LwqqGroup* group = find_group_by_gid(m_lc, id.toUtf8().constData());
    LwqqConfirmTable* ct =(LwqqConfirmTable*) s_malloc0(sizeof(*ct));
    ct->title = s_strdup(_("Block Setting"));
    ct->no_label = s_strdup(_("No Block"));
    ct->yes_label = s_strdup(_("Slience Receive"));
    ct->exans_label = s_strdup(_("Block"));
    ct->flags |= LWQQ_CT_CHOICE_MODE|LWQQ_CT_ENABLE_IGNORE;
    ct->answer = (LwqqAnswer)group->mask;
    ct->cmd = _C_(3p,set_cgroup_block,ct,m_lc,group);
    ac_show_confirm_table(m_lc, ct, NULL);
}

static void cb_need_verify2(LwqqClient* lc,LwqqVerifyCode** code)
{
    CallbackObject cb;
    cb.fun_t = NEED_VERIFY2;
    cb.ptr1 = (void *)lc;
    cb.ptr2 = (void *)(*code);
    ObjectInstance::instance()->callSignal(cb);
}

static void cb_login_stage_1(LwqqClient* lc,LwqqErrorCode* err)
{
    CallbackObject cb;
    cb.fun_t = LOGIN_COMPLETE;
    cb.ptr1 = (void *)lc;
    cb.ptr2= (void *)err;
    ObjectInstance::instance()->callSignal(cb);
} 

static void cb_login_stage_2(LwqqAsyncEvent* event,LwqqClient* lc)
{
    CallbackObject cb;
    cb.fun_t = LOGIN_STAGE_2;
    cb.ptr1 = (void *)event;
    cb.ptr2 = (void *)lc;
    ObjectInstance::instance()->callSignal(cb);
}

static void cb_login_stage_3(LwqqClient* lc)
{
    CallbackObject cb;
    cb.fun_t = LOGIN_STAGE_3;
    cb.ptr1 = (void *)lc;
    ObjectInstance::instance()->callSignal(cb);
}

void cb_login_stage_f(LwqqClient* lc)
{
    CallbackObject cb;
    cb.fun_t = LOGIN_STAGE_f;
    cb.ptr1 = (void *)lc;
    ObjectInstance::instance()->callSignal(cb);
}


void cb_friend_avatar(LwqqClient* lc,LwqqBuddy* buddy)
{
    CallbackObject cb;
    cb.fun_t = FRIEND_AVATAR;
    cb.ptr1 = (void *)lc;
    cb.ptr2 = (void*)buddy;
    ObjectInstance::instance()->callSignal(cb);
}

void cb_group_avatar(LwqqClient *lc, LwqqGroup *group)
{
    CallbackObject cb;
    cb.fun_t = GROUP_AVATAR;
    cb.ptr1 = (void *)lc;
    cb.ptr2 = (void*)group;
    ObjectInstance::instance()->callSignal(cb);
}

void cb_group_members(LwqqClient *lc, LwqqGroup *group)
{
    CallbackObject cb;
    cb.fun_t = GROUP_MEMBERS;
    cb.ptr1 = (void *)lc;
    cb.ptr2 = (void*)group;
    ObjectInstance::instance()->callSignal(cb);
}

void cb_qq_msg_check(LwqqClient* lc)
{
    CallbackObject cb;
    cb.fun_t = QQ_MSG_CHECK;
    cb.ptr1 = (void *)lc;
    ObjectInstance::instance()->callSignal(cb);
}

void cb_show_confirm_table(LwqqClient *lc, LwqqConfirmTable *table, add_info *info)
{
    CallbackObject cb;
    cb.fun_t = SHOW_CONFIRM;
    cb.ptr1 = (void *)lc;
    cb.ptr2 = (void*)table;
    cb.ptr3 = (void*)info;
    ObjectInstance::instance()->callSignal(cb);
}

void cb_rewrite_whole_message_list(LwqqAsyncEvent *ev, qq_account *ac, LwqqGroup *group)
{
    CallbackObject cb;
    cb.fun_t = REWRITE_MSG;
    cb.ptr1 = (void *)ev;
    cb.ptr2 = (void*)ac;
    cb.ptr3 = (void*)group;
    ObjectInstance::instance()->callSignal(cb);
}

void cb_show_messageBox(msg_type type, const char *title, const char *message)
{
    CallbackObject cb;
    cb.fun_t = SHOW_MESSAGE;
    cb.ptr1 = (void *)title;
    cb.ptr2 = (void*)message;
    cb.type = type;
    ObjectInstance::instance()->callSignal(cb);
}

void cb_return_friend_come(LwqqClient *lc, LwqqBuddy *b)
{
    CallbackObject cb;
    cb.fun_t = FRIEND_COME;
    cb.ptr1 = (void *)lc;
    cb.ptr2 = (void*)b;
    ObjectInstance::instance()->callSignal(cb);
}

void cb_qq_set_group_name(LwqqGroup *group)
{
    CallbackObject cb;
    cb.fun_t = SET_GROUPNAME;
    cb.ptr1 = (void *)group;
    ObjectInstance::instance()->callSignal(cb);
}

void cb_display_user_info(qq_account *ac, LwqqBuddy *b, char *who)
{
    CallbackObject cb;
    cb.fun_t = GET_USERINFO;
    cb.ptr1 = (void *)ac;
    cb.ptr2 = (void*)b;
    cb.ptr3 = (void*)who;
    ObjectInstance::instance()->callSignal(cb);
}

static void verify_required_confirm(LwqqClient* lc,char* account,LwqqConfirmTable* ct)
{
    if(ct->answer == LWQQ_NO)
        lwqq_info_answer_request_friend(lc, account, ct->answer, ct->input);
    else if(ct->answer == LWQQ_IGNORE){
        //ignore it.
    }else
        lwqq_info_answer_request_friend(lc, account, ct->answer, NULL);
    lwqq_ct_free(ct);
    s_free(account);
}


static void friends_valid_hash(LwqqAsyncEvent* ev)
{
    LwqqClient* lc = ev->lc;
    qq_account* ac = (qq_account*)lc->data;
    if(ev->result == LWQQ_EC_HASH_WRONG){
        KMessageBox::queuedMessageBox(Kopete::UI::Global::mainWidget(), KMessageBox::Error, \
                                      i18n("Hash Function Wrong, Please try again later or report to author"));
        return;
    }
    if(ev->result != int(LWQQ_EC_OK)){
        KMessageBox::queuedMessageBox(Kopete::UI::Global::mainWidget(), KMessageBox::Error, \
                                      i18n("Get Friend List Failed"));
        return;
    }

    const LwqqHashEntry* succ_hash = lwqq_hash_get_last(lc);
    lwdb_userdb_write(ac->db, "last_hash", succ_hash->name);
    LwqqAsyncEvent* event = lwqq_info_get_group_name_list(lc, NULL, NULL);
    lwqq_async_add_event_listener(event,_C_(2p,cb_login_stage_2,event,lc));
}

static void get_friends_info_retry(LwqqClient* lc,LwqqHashFunc hashtry)
{
    kDebug(WEBQQ_GEN_DEBUG)<<"get_friends_info_retry";
    LwqqAsyncEvent* ev;
    qq_account* ac = (qq_account*)lc->data;
    ev = lwqq_info_get_friends_info(lc,hashtry,ac->js);
    lwqq_async_add_event_listener(ev, _C_(2p,friends_valid_hash,ev,hashtry));
    kDebug(WEBQQ_GEN_DEBUG)<<"has called get_friends_info_retry";
}

//find buddy and group ,add they

static void confirm_table_yes(LwqqConfirmTable* table,const char *input, LwqqAnswer answer)
{
    if(table->exans_label){
        table->answer = answer;
    }else
        table->answer = LWQQ_YES;
    if(table->input_label){
        table->input = s_strdup(input);
    }
    vp_do(table->cmd,NULL);
}
static void confirm_table_no(LwqqConfirmTable* table,const char *input)
{
    table->answer = (table->flags&LWQQ_CT_ENABLE_IGNORE)?LWQQ_IGNORE:LWQQ_NO;
    if(table->input_label){
        table->input = s_strdup(input);
    }
    vp_do(table->cmd,NULL);
}


static void add_friend_receipt(LwqqAsyncEvent* ev)
{
    int err = ev->result;
    LwqqClient* lc = ev->lc;
    if(err == 6 ){
        cb_show_messageBox(MSG_ERROR, s_strdup("Error Message"), s_strdup("ErrCode:6\nPlease try agagin later\n"));
    }
}

static void add_friend_ask_message(LwqqClient* lc,LwqqConfirmTable* ask,LwqqBuddy* b)
{
    add_friend(lc,ask,b,s_strdup(ask->input));
}

static void add_friend(LwqqClient* lc,LwqqConfirmTable* c,LwqqBuddy* b,char* message)
{
    if(c->answer == LWQQ_NO){
        goto done;
    }
    if(message==NULL){
        LwqqConfirmTable* ask = (LwqqConfirmTable*)s_malloc0(sizeof(*ask));
        ask->input_label = s_strdup(_("Invite Message"));
        ask->cmd = _C_(3p,add_friend_ask_message,lc,ask,b);
        add_info *info = (add_info*)s_malloc0(sizeof(*info));
        info->qq = s_strdup(b->qqnumber);
        info->name = s_strdup(b->nick);
        info->uin = s_strdup(b->uin);
        cb_show_confirm_table(lc, ask, info);
        lwqq_ct_free(c);
        return ;
    }else{
        LwqqAsyncEvent* ev = lwqq_info_add_friend(lc, b,message);
        lwqq_async_add_event_listener(ev, _C_(p,add_friend_receipt,ev));
    }
done:
    lwqq_ct_free(c);
    lwqq_buddy_free(b);
    s_free(message);
}


static void format_body_from_buddy(char* body,size_t buf_len,LwqqBuddy* buddy)
{
    char body_[1024] = {0};
#define ADD_INFO(k,v)  if(v) format_append(body_,"%s:%s\n",k,v)
    ADD_INFO(_("QQ"), buddy->qqnumber);
    ADD_INFO(_("Nick"), buddy->nick);
    ADD_INFO(_("Longnick"), buddy->personal);
    ADD_INFO(_("Gender"), qq_gender_to_str(buddy->gender));
    ADD_INFO(_("Shengxiao"), qq_shengxiao_to_str(buddy->shengxiao));
    ADD_INFO(_("Constellation"), qq_constel_to_str(buddy->constel));
    ADD_INFO(_("Blood Type"), qq_blood_to_str(buddy->blood));
    ADD_INFO(_("Birthday"),lwqq_date_to_str(buddy->birthday));
    ADD_INFO(_("Country"), buddy->country);
    ADD_INFO(_("Province"), buddy->province);
    ADD_INFO(_("City"), buddy->city);
    ADD_INFO(_("Phone"), buddy->phone);
    ADD_INFO(_("Mobile"), buddy->mobile);
    ADD_INFO(_("Email"), buddy->email);
    ADD_INFO(_("Occupation"), buddy->occupation);
    ADD_INFO(_("College"), buddy->college);
    ADD_INFO(_("Homepage"), buddy->homepage);
#undef ADD_INFO
    strncat(body,body_,buf_len-strlen(body));
}

static void system_message(LwqqClient* lc,LwqqMsgSystem* system,LwqqBuddy* buddy)
{
    qq_account* ac = (qq_account*)lwqq_client_userdata(lc);
    char buf1[256]={0};
    if(system->type == LwqqMsgSystem::VERIFY_REQUIRED) {
        char buf2[2048];
        LwqqConfirmTable* ct =(LwqqConfirmTable*)s_malloc0(sizeof(*ct));
        ct->title = s_strdup(_("Friend Confirm"));
        snprintf(buf2,sizeof(buf2),
                 _("%s\nRequest as your friend\nAdditional Reason:%s\n\n"),system->account,system->verify_required.msg);
        format_body_from_buddy(buf2,sizeof(buf2),buddy);
        ct->body = s_strdup(buf2);
        ct->exans_label = s_strdup(_("Agree and add back"));
        ct->input_label = s_strdup(_("Refuse reason"));
        ct->flags = LWQQ_CT_ENABLE_IGNORE;
        ct->cmd = _C_(3p,verify_required_confirm,lc,s_strdup(system->account),ct);
        add_info *info = (add_info*)s_malloc0(sizeof(*info));
        info->qq = s_strdup(buddy->qqnumber);
        info->name = s_strdup(buddy->nick);
        info->uin = s_strdup(buddy->uin);
        cb_show_confirm_table(lc, ct, info);
        lwqq_buddy_free(buddy);
        lwqq_msg_free((LwqqMsg*)system);

    } else if(system->type == LwqqMsgSystem::VERIFY_PASS_ADD) {
        snprintf(buf1,sizeof(buf1),_("%s accept your request,and add back you as friend too"),system->account);
        cb_show_messageBox(MSG_ADD, s_strdup("Add Friend"), s_strdup(buf1));
    } else if(system->type == LwqqMsgSystem::VERIFY_PASS) {
        snprintf(buf1,sizeof(buf1),_("%s accept your request"),system->account);
        cb_show_messageBox(MSG_INFO, s_strdup("Add Friend"), s_strdup(buf1));
    }
}

static void search_buddy_receipt(LwqqAsyncEvent* ev,LwqqBuddy* buddy,char* uni_id,char* message)
{
    int err = ev->result;
    LwqqClient* lc = ev->lc;
    LwqqConfirmTable* confirm = (LwqqConfirmTable*)s_malloc0(sizeof(*confirm));
    add_info *info = (add_info*)s_malloc0(sizeof(*info));
    char body[1024] = {0};
    //captcha wrong
    if(err == 10000){
        LwqqAsyncEvent* event = lwqq_info_search_friend(lc,uni_id,buddy);
        lwqq_async_add_event_listener(event, _C_(4p,search_buddy_receipt,event,buddy,uni_id,message));
        return;
    }
    if(err == LWQQ_EC_NO_RESULT){
        cb_show_messageBox(MSG_ERROR, s_strdup("Error Message"), s_strdup("Account not exists or not main display account"));
        goto failed;
    }
    if(!buddy->token){
        cb_show_messageBox(MSG_ERROR, s_strdup("Error Message"), s_strdup("Get friend infomation failed"));
        goto failed;
    }

    confirm->title = s_strdup(_("Friend Confirm"));

    format_body_from_buddy(body,sizeof(body),buddy);
    confirm->body = s_strdup(body);
    confirm->cmd = _C_(4p,add_friend,lc,confirm,buddy,message);
    info->qq = s_strdup(buddy->qqnumber);
    info->name = s_strdup(buddy->nick);
    info->uin = s_strdup(buddy->uin);
    cb_show_confirm_table(lc,confirm, info);
    s_free(uni_id);
    return;
failed:
    lwqq_buddy_free(buddy);
    s_free(message);
    s_free(uni_id);
}

void qq_add_buddy( LwqqClient* lc, const char *username, const char *message)
{
    const char* uni_id = username;
    LwqqGroup* g = NULL;
    LwqqBuddy* f_buddy = lwqq_buddy_new();
    LwqqFriendCategory* cate = NULL;
    if(cate == NULL){
        f_buddy->cate_index = 0;
    }else
        f_buddy->cate_index = cate->index;
    //friend->qqnumber = s_strdup(qqnum);
    LwqqAsyncEvent* ev = lwqq_info_search_friend(lc,uni_id,f_buddy);
    const char* msg = NULL;
    lwqq_async_add_event_listener(ev, _C_(4p,search_buddy_receipt,ev,f_buddy,s_strdup(uni_id),s_strdup(msg)));

}

static void add_group_receipt(LwqqAsyncEvent* ev,LwqqGroup* g)
{
    int err = ev->result;
    LwqqClient* lc = ev->lc;
    if(err == 6 ){
        cb_show_messageBox(MSG_ERROR, s_strdup("Error Message"), s_strdup("ErrCode:6\nPlease try agagin later\n"));
    }
    lwqq_group_free(g);
}
static void add_group(LwqqClient* lc,LwqqConfirmTable* c,LwqqGroup* g)
{
    LwqqAsyncEvent* ev = lwqq_info_add_group(lc, g, c->input);
    if(c->answer == LWQQ_NO){
        goto done;
    }

    lwqq_async_add_event_listener(ev, _C_(2p,add_group_receipt,ev,g));
done:
    lwqq_ct_free(c);
}
static void search_group_receipt(LwqqAsyncEvent* ev,LwqqGroup* g)
{
    int err = ev->result;
    LwqqClient* lc = ev->lc;
    if(err == 10000){
        LwqqAsyncEvent* event = lwqq_info_search_group_by_qq(lc,g->qq,g);
        lwqq_async_add_event_listener(event, _C_(2p,search_group_receipt,ev,g));
        return;
    }
    if(err == LWQQ_EC_NO_RESULT){
        cb_show_messageBox(MSG_ERROR, s_strdup("Error Message"), s_strdup("Get QQ Group Infomation Failed"));
        lwqq_group_free(g);
        return;
    }
    LwqqConfirmTable* confirm = (LwqqConfirmTable*)s_malloc0(sizeof(*confirm));
    confirm->title = s_strdup(_("Confirm QQ Group"));
    confirm->input_label = s_strdup(_("Additional Reason"));
    char body[1024] = {0};
#define ADD_INFO(k,v)  format_append(body,_(k":%s\n"),v)
    ADD_INFO("QQ",g->qq);
    ADD_INFO("Name",g->name);
#undef ADD_INFO
    confirm->body = s_strdup(body);
    confirm->cmd = _C_(3p,add_group,lc,confirm,g);
    add_info *info = (add_info*)s_malloc0(sizeof(*info));
    info->qq = s_strdup(g->qq);
    info->name = s_strdup(g->name);
    info->uin = s_strdup(g->gid);
    cb_show_confirm_table(lc,confirm, info);
}

static void search_group(LwqqClient* lc,const char* text)
{
    LwqqGroup* g = lwqq_group_new(LWQQ_GROUP_QUN);
    LwqqAsyncEvent* ev;
    ev = lwqq_info_search_group_by_qq(lc, text, g);
    lwqq_async_add_event_listener(ev, _C_(2p,search_group_receipt,ev,g));
}
static void do_no_thing(void* data,const char* text)
{
}
static void qq_add_group(LwqqClient* lc, const char *name)
{
    search_group(lc, name);
}

static void write_buddy_to_db(LwqqClient* lc,LwqqBuddy* b)
{
    qq_account* ac = (qq_account*)lwqq_client_userdata(lc);

    lwdb_userdb_insert_buddy_info(ac->db, &b);
    cb_return_friend_come(lc, b);
}

static void set_cgroup_block(LwqqConfirmTable* ct,LwqqClient* lc,LwqqGroup* g)
{
    if(ct->answer != LWQQ_IGNORE){
        lwqq_async_add_event_listener(
                    lwqq_info_mask_group(lc,g,(int)ct->answer),
                    _C_(p,cb_qq_set_group_name,g));
    }
    lwqq_ct_free(ct);
}

static void modify_self_longnick(LwqqClient* lc,LwqqConfirmTable* ct)
{
    if(ct->answer == LWQQ_YES){
        const char* longnick = ct->input;
        lwqq_info_set_self_long_nick(lc, longnick);
    }
    lwqq_ct_free(ct);
}

static void display_self_longnick(LwqqClient* lc)
{
    LwqqConfirmTable* ct = s_malloc0(sizeof(*ct));
    ct->title = s_strdup(_("Modify Self Longnick"));
    ct->input_label = s_strdup("Longnick");
    ct->input = s_strdup(lc->myself->long_nick);
    ct->cmd = _C_(2p,modify_self_longnick,lc,ct);
    cb_show_confirm_table(lc, ct, NULL);
}





static void cb_friend_come(LwqqClient* lc,LwqqBuddy** buddy){printf("********%s\n", __FUNCTION__);}
static void cb_group_come(LwqqClient* lc,LwqqGroup** group){printf("********%s\n", __FUNCTION__);}

static void cb_lost_connection(LwqqClient* lc){printf("********%s\n", __FUNCTION__);}
static void cb_upload_content_fail(LwqqClient* lc,const char** serv_id,LwqqMsgContent** c,int err)
{printf("********%s\n", __FUNCTION__);}
static void cb_delete_group_local(LwqqClient* lc,const LwqqGroup** g){printf("********%s\n", __FUNCTION__);}
static void cb_flush_group_members(LwqqClient* lc,LwqqGroup** g){printf("********%s\n", __FUNCTION__);}


Kopete::OnlineStatus statusFromLwqqStatus(LwqqStatus status)
{
    switch(status) {
    case LWQQ_STATUS_LOGOUT:
        return WebqqProtocol::protocol()->WebqqLogout;
    case LWQQ_STATUS_ONLINE:
        return WebqqProtocol::protocol()->WebqqOnline;
    case LWQQ_STATUS_OFFLINE:
        return WebqqProtocol::protocol()->WebqqOffline;
    case LWQQ_STATUS_AWAY:
        return WebqqProtocol::protocol()->WebqqAway;
    case LWQQ_STATUS_HIDDEN:
        return WebqqProtocol::protocol()->WebqqHidden;
    case LWQQ_STATUS_BUSY:
        return WebqqProtocol::protocol()->WebqqBusy;
    case LWQQ_STATUS_CALLME:
        return WebqqProtocol::protocol()->WebqqCallme;
    default:
        return WebqqProtocol::protocol()->WebqqOffline;
    }
    
}


#include "qqaccount.moc"
