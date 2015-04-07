/*
    qqchatsession.cpp - Manages friend and session chats

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

#include "qqchatsession.h"

#include <qlabel.h>
#include <qimage.h>

#include <qfile.h>
#include <qicon.h>
#include <QFileDialog>
//Added by qt3to4:
#include <QPixmap>
#include <QList>

#include <kconfig.h>
#include <kdebug.h>
#include <kinputdialog.h>
#include <klocale.h>
#include <kmessagebox.h>
#include <kmenu.h>
#include <ktemporaryfile.h>
#include <kxmlguiwindow.h>
#include <ktoolbar.h>
#include <krun.h>
#include <kiconloader.h>
#include <kicon.h>

//#include "kopetecontactaction.h"
//#include "kopetemetacontact.h"
//#include "kopetecontactlist.h"
#include "kopetechatsessionmanager.h"
#include "kopeteprotocol.h"
//#include "kopeteuiglobal.h"
//#include "kopeteglobal.h"
//#include "kopeteview.h"

#include "qqcontact.h"
#include "qqaccount.h"
#include <kactioncollection.h>

QQChatSession::QQChatSession( Kopete::Protocol *protocol, const Kopete::Contact *user,
	Kopete::ContactPtrList others )
: Kopete::ChatSession( user, others, protocol )
{	
    setComponentData(protocol->componentData());
    Kopete::ChatSessionManager::self()->registerChatSession( this );
	// Add Actions
	KAction *buzzAction = new KAction( KIcon("bell"), i18n( "Buzz Contact" ), this );
        actionCollection()->addAction( "QQBuzz", buzzAction );
    //buzzAction->setShortcut( KShortcut("Ctrl+G") );
	connect( buzzAction, SIGNAL(triggered(bool)), this, SLOT(slotBuzzContact()) );

    KAction *imageAction = new KAction( KIcon("image"), i18n( "Image send" ), this );
        actionCollection()->addAction( "QQimage", imageAction );
    //buzzAction->setShortcut( KShortcut("Ctrl+G") );
    connect( imageAction, SIGNAL(triggered(bool)), this, SLOT(slotimageContact()) );

	KAction *userInfoAction = new KAction( KIcon("help-about"), i18n( "Show User Info" ), this );
        actionCollection()->addAction( "QQShowInfo",  userInfoAction) ;
	connect( userInfoAction, SIGNAL(triggered(bool)), this, SLOT(slotUserInfo()) );


//	if(c->hasProperty(Kopete::Global::Properties::self()->photo().key())  )
//	{
//		connect( Kopete::ChatSessionManager::self() , SIGNAL(viewActivated(KopeteView*)) , this, SLOT(slotDisplayPictureChanged()) );
//	}
//	else
//	{
//		m_image = 0L;
//	}

    setXMLFile("qqchatui.rc");
}

QQChatSession::~QQChatSession()
{

}

void QQChatSession::slotBuzzContact()
{
	QList<Kopete::Contact*>contacts = members();
    static_cast<QQContact *>(contacts.first())->buzzContact();
}

void QQChatSession::slotimageContact()
{

    QString fileName = QFileDialog::getOpenFileName(NULL, tr("Open File"),
                                                    QDir::homePath(),
                                                     tr("Images (*.png *.xpm *.jpg *.gif *.bmp *jpeg)"));
    if(!fileName.isNull())
    {
        QList<Kopete::Contact*>contacts = members();
        static_cast<QQContact *>(contacts.first())->imageContact(fileName);
    }
}

void QQChatSession::setTopic(const QString &topic)
{
     setDisplayName(i18n("%1", topic));
}

void QQChatSession::slotUserInfo()
{
	QList<Kopete::Contact*>contacts = members();
    static_cast<QQContact *>(contacts.first())->slotUserInfo();
}


void QQChatSession::slotSendFile()
{
	QList<Kopete::Contact*>contacts = members();
    static_cast<QQContact *>(contacts.first())->sendFile();
}

void QQChatSession::slotDisplayPictureChanged()
{
	QList<Kopete::Contact*> mb=members();
    QQContact *c = static_cast<QQContact *>( mb.first() );
}

#include "qqchatsession.moc"
