/*
 * Copyright (C) 2017  Sergey Ilinykh
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301  USA
 *
 */

#include "irisnetplugin.h"

#include <QNetworkInterface>

namespace XMPP {

class IrisQtName : public NameProvider
{
	Q_OBJECT
	Q_INTERFACES(XMPP::NameProvider)

	int currentId;
	QHash<int,QDnsLookup*> lookups;

public:
	IrisQtName(QObject *parent = 0) :
	    NameProvider(parent),
	    currentId(0)
	{

	}

	~IrisQtName()
	{
		qDeleteAll(lookups);
	}

	bool supportsSingle() const
	{
		return true;
	}

	bool supportsRecordType(int type) const
	{
		// yes the types matched to ones from jdns, so it's fine.
		static QVector<int> types = {
		    QDnsLookup::A, QDnsLookup::AAAA, QDnsLookup::ANY,
		    QDnsLookup::CNAME, QDnsLookup::MX, QDnsLookup::NS,
		    QDnsLookup::PTR, QDnsLookup::SRV, QDnsLookup::TXT};
		return types.contains(type);
	}

	int resolve_start(const QByteArray &name, int qType, bool longLived)
	{
		Q_UNUSED(longLived); // FIXME handle local like in jdns name provider
        int id = currentId++;

        // check if it's A/AAAA. QDnsLookup fails to handle this in some cases.
        QHostAddress addr(QString::fromLatin1(name));
        if (!addr.isNull()) {
            QList<XMPP::NameRecord> results;
            XMPP::NameRecord r;
            r.setAddress(addr);
            results.append(r);
            QMetaObject::invokeMethod(this, "resolve_resultsReady", Qt::QueuedConnection,
                                      Q_ARG(int, id), Q_ARG(QList<XMPP::NameRecord>, results));
        } else {
            QDnsLookup *lookup = new QDnsLookup((QDnsLookup::Type)qType, QString::fromLatin1(name), this);
            connect(lookup, SIGNAL(finished()), this, SLOT(handleLookup()));
            lookup->setProperty("iid", id);
            lookups.insert(id, lookup);
            QMetaObject::invokeMethod(lookup, "lookup", Qt::QueuedConnection);
        }
		return id;
	}

	void resolve_stop(int id)
	{
		QDnsLookup *lookup = lookups.value(id);
		if (lookup) {
			lookup->abort(); // handleLookup will catch it and delete
		}
	}

private slots:
	void handleLookup()
	{
		QDnsLookup *lookup = static_cast<QDnsLookup *>(sender());
		int id = lookup->property("iid").toInt();
		lookups.remove(id);
		if (lookup->error() != QDnsLookup::NoError) {
			XMPP::NameResolver::Error e;
			switch (lookup->error()) {
				case QDnsLookup::InvalidReplyError:
					e = XMPP::NameResolver::ErrorTimeout;
					break;
				case QDnsLookup::NotFoundError:
					e = XMPP::NameResolver::ErrorNoName;
					break;
				case QDnsLookup::ResolverError:
				case QDnsLookup::OperationCancelledError:
				case QDnsLookup::InvalidRequestError:
				case QDnsLookup::ServerFailureError:
				case QDnsLookup::ServerRefusedError:
				default:
					e = XMPP::NameResolver::ErrorGeneric;
					break;
			}
			if (lookup->error() != QDnsLookup::OperationCancelledError) { // don't report after resolve_stop()
				emit resolve_error(id, e);
			}
			lookup->deleteLater();
			return;
		}

		QList<XMPP::NameRecord> results;
		for (auto &qtr: lookup->hostAddressRecords()) {
			XMPP::NameRecord ir(qtr.name().toLatin1(), qtr.timeToLive());
			ir.setAddress(qtr.value());
			results += ir;
		}
		for (auto &qtr: lookup->mailExchangeRecords()) {
			XMPP::NameRecord ir(qtr.name().toLatin1(), qtr.timeToLive());
			ir.setMx(qtr.exchange().toLatin1(), qtr.preference());
			results += ir;
		}
		for (auto &qtr: lookup->nameServerRecords()) {
			XMPP::NameRecord ir(qtr.name().toLatin1(), qtr.timeToLive());
			ir.setNs(qtr.value().toLatin1());
			results += ir;
		}
		for (auto &qtr: lookup->pointerRecords()) {
			XMPP::NameRecord ir(qtr.name().toLatin1(), qtr.timeToLive());
			ir.setPtr(qtr.value().toLatin1());
			results += ir;
		}
		for (auto &qtr: lookup->canonicalNameRecords()) {
			XMPP::NameRecord ir(qtr.name().toLatin1(), qtr.timeToLive());
			ir.setCname(qtr.value().toLatin1());
			results += ir;
		}
		for (auto &qtr: lookup->serviceRecords()) {
			XMPP::NameRecord ir(qtr.name().toLatin1(), qtr.timeToLive());
			ir.setSrv(qtr.target().toLatin1(),qtr.port(),qtr.priority(),qtr.weight());
			results += ir;
		}
		for (auto &qtr: lookup->textRecords()) {
			XMPP::NameRecord ir(qtr.name().toLatin1(), qtr.timeToLive());
			ir.setTxt(qtr.values());
			results += ir;
		}
		lookup->deleteLater();
		emit resolve_resultsReady(id, results);
	}
};

class IrisQtNameProvider : public IrisNetProvider
{
	Q_OBJECT
	Q_INTERFACES(XMPP::IrisNetProvider)
public:

	NameProvider *createNameProviderInternet()
	{
		return new IrisQtName;
	}
};

IrisNetProvider *irisnet_createQtNameProvider()
{
	return new IrisQtNameProvider;
}

}

#include "netinterface_qtname.moc"
