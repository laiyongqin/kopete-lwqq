/*
    qqdiscuchatsession.cpp - Manages Discussion chats

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

#include <kdebug.h>
#include <klineedit.h>
#include <klocale.h>
#include <kmessagebox.h>
#include <kmenu.h>
#include <kconfig.h>

#include <kopetecontactaction.h>
#include <kopetecontactlist.h>
#include <kopetecontact.h>
#include <kopetechatsessionmanager.h>
#include <kopeteuiglobal.h>
#include <kicon.h>

#include "qqdiscuchatsession.h"
#include "qqcontact.h"
#include "qqaccount.h"
#include "kopeteprotocol.h"
//#include "qqinvitelistimpl.h"
#include <kactioncollection.h>

QQDiscuChatSession::QQDiscuChatSession(Kopete::Protocol *protocol, const Kopete::Contact *user,
	Kopete::ContactPtrList others )
: Kopete::ChatSession( user, others, protocol )
{

	Kopete::ChatSessionManager::self()->registerChatSession( this );
	setComponentData(protocol->componentData());

    //m_qqRoom = qqRoom;

//	m_actionInvite = new KAction( KIcon("x-office-contact"), i18n( "&Invite others" ), this ); // icon should probably be "contact-invite", but that doesn't exist... please request an icon on http://techbase.kde.org/index.php?title=Projects/Oxygen/Missing_Icons
//        actionCollection()->addAction( "qqInvite", m_actionInvite );
//	connect ( m_actionInvite, SIGNAL(triggered(bool)), this, SLOT(slotInviteOthers()) );

    setXMLFile("qqdiscuui.rc");
}

QQDiscuChatSession::~QQDiscuChatSession()
{
    //emit leavingConference( this );
}

QQAccount *QQDiscuChatSession::account()
{
    return static_cast< QQAccount *>( Kopete::ChatSession::account() );
}

const QString &QQDiscuChatSession::room()
{
    return m_qqRoom;
}

void QQDiscuChatSession::joined( QQContact *c )
{
	addContact( c );
}

void QQDiscuChatSession::left( QQContact *c )
{
	removeContact( c );
}

void QQDiscuChatSession::removeAllContacts()
{
    Kopete::ContactPtrList m = members();
    foreach( Kopete::Contact *c, m )
    {
        removeContact( c );
    }
}

void QQDiscuChatSession::setTopic( const QString & topic )
{
    setDisplayName(topic);
}

//void QQDiscuChatSession::slotMessageSent( Kopete::Message & message, Kopete::ChatSession * )
//{

//    QQAccount *acc = dynamic_cast< QQAccount *>( account() );
//	if( acc )
//		acc->sendConfMessage( this, message );
//	appendMessage( message );
//	messageSucceeded();
//}

//void QQDiscuChatSession::slotInviteOthers()
//{
//	QStringList buddies;

//	QHash<QString, Kopete::Contact*>::ConstIterator it, itEnd = account()->contacts().constEnd();
//	for( it = account()->contacts().constBegin(); it != itEnd; ++it )
//	{
//		if( !members().contains( it.value() ) )
//			buddies.push_back( it.value()->contactId() );
//	}

//    QQInviteListImpl *dlg = new QQInviteListImpl( Kopete::UI::Global::mainWidget() );
//	QObject::connect( dlg, SIGNAL(readyToInvite(QString,QStringList,QStringList,QString)),
//				account(), SLOT(slotAddInviteConference(QString,QStringList,QStringList,QString)) );
//    dlg->setRoom( m_qqRoom );
//	dlg->fillFriendList( buddies );
//	for( QList<Kopete::Contact*>::ConstIterator it = members().constBegin(); it != members().constEnd(); ++it )
//		dlg->addParticipant( (*it)->contactId() );
//	dlg->show();
//}

#include "qqdiscuchatsession.moc"

// vim: set noet ts=4 sts=4 sw=4:

