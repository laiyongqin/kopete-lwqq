/*
    qqgroupchatsession.h - Manages group chats

    Copyright (c) 2014      by Jun Zhang		 <jun.zhang@i-soft.com.cn>
    Copyright (c) 2002      by Duncan Mac-Vicar Prett <duncan@kde.org>
    Copyright (c) 2002      by Daniel Stone           <dstone@kde.org>
    Copyright (c) 2002-2003 by Martijn Klingens       <klingens@kde.org>
    Copyright (c) 2002-2004 by Olivier Goffart        <ogoffart@kde.org>
    Copyright (c) 2003      by Jason Keirstead        <jason@keirstead.org>
    Copyright (c) 2005      by Michaël Larouche      <larouche@kde.org>
    Copyright (c) 2009      by Fabian Rami          <fabian.rami@wowcompany.com>

    Kopete    (c) 2002-2003 by the Kopete developers  <kopete-devel@kde.org>

    *************************************************************************
    *                                                                       *
    * This library is free software; you can redistribute it and/or         *
    * modify it under the terms of the GNU Lesser General Public            *
    * License as published by the Free Software Foundation; either          *
    * version 2 of the License, or (at your option) any later version.      *
    *                                                                       *
    *************************************************************************
*/

#ifndef QQGroupChatSession_H
#define QQGroupChatSession_H

#include "kopetechatsession.h"

class QQContact;
class QQAccount;

/**
 * @author André Duffeck
 */
class QQGroupChatSession : public Kopete::ChatSession
{
	Q_OBJECT
public:
    QQGroupChatSession( Kopete::Protocol *protocol, const Kopete::Contact *user, Kopete::ContactPtrList others );
    ~QQGroupChatSession();

    void joined( QQContact *c);
    void left( QQContact *c );
    QQAccount *account();
	void setTopic( const QString & topic );	

	void removeAllContacts();
private slots:
    void slotimageContact();
    void slotUserInfo();
signals:
    void leavingChat( QQGroupChatSession *s );
private:
	QString m_topic;
};

#endif

// vim: set noet ts=4 sts=4 tw=4:

