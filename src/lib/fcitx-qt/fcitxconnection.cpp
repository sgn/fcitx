#include "fcitxconnection_p.h"
#include "fcitx-config/xdg.h"
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QDBusReply>
#include <QDBusConnectionInterface>

#include "module/dbus/dbusstuff.h"
#include "fcitx-utils/utils.h"
#include <QX11Info>
#include <QFile>
#include <qtimer.h>
#include <QDir>
#include <X11/Xlib.h>

FcitxConnection::FcitxConnection(QObject* parent): QObject(parent)
    ,d_ptr(new FcitxConnectionPrivate(this))
{
}

void FcitxConnection::startConnection()
{
    Q_D(FcitxConnection);
    if (!d->m_initialized) {
        d->initialize();
        d->createConnection();
    }
}

void FcitxConnection::endConnection()
{
    Q_D(FcitxConnection);
    d->cleanUp();
    d->finalize();
    d->m_connectedOnce = false;
}

bool FcitxConnection::autoReconnect()
{
    Q_D(FcitxConnection);
    return d->m_autoReconnect;
}

void FcitxConnection::setAutoReconnect(bool a)
{
    Q_D(FcitxConnection);
    d->m_autoReconnect = a;
}

QDBusConnection* FcitxConnection::connection()
{
    Q_D(FcitxConnection);
    return d->m_connection;
}

const QString& FcitxConnection::serviceName()
{
    Q_D(FcitxConnection);
    return d->m_serviceName;
}

bool FcitxConnection::isConnected()
{
    Q_D(FcitxConnection);
    return d->isConnected();
}



FcitxConnection::~FcitxConnection()
{

}

FcitxConnectionPrivate::FcitxConnectionPrivate(FcitxConnection* conn) : QObject(conn)
    ,q_ptr(conn)
    ,m_displayNumber(-1)
    ,m_serviceName(QString("%1-%2").arg(FCITX_DBUS_SERVICE).arg(displayNumber()))
    ,m_connection(0)
    ,m_serviceWatcher(new QDBusServiceWatcher(conn))
    ,m_watcher(new QFileSystemWatcher(this))
    ,m_autoReconnect(true)
    ,m_connectedOnce(false)
    ,m_initialized(false)
{
#if QT_VERSION < QT_VERSION_CHECK(4, 8, 0)
    connect(qApp, SIGNAL(aboutToQuit()), m_watcher.data(), SLOT(deleteLater()));
#endif
}

FcitxConnectionPrivate::~FcitxConnectionPrivate()
{
    if (!m_watcher.isNull())
        delete m_watcher.data();
}

void FcitxConnectionPrivate::initialize() {
    m_serviceWatcher->setConnection(QDBusConnection::sessionBus());
    m_serviceWatcher->addWatchedService(m_serviceName);

    QFileInfo info(socketFile());
    QDir dir(info.path());
    if (!dir.exists()) {
        QDir rt(QDir::root());
        rt.mkpath(info.path());
    }
    m_watcher.data()->addPath(info.path());
    if (info.exists()) {
        m_watcher.data()->addPath(info.filePath());
    }

    connect(m_watcher.data(), SIGNAL(fileChanged(QString)), this, SLOT(socketFileChanged()));
    connect(m_watcher.data(), SIGNAL(directoryChanged(QString)), this, SLOT(socketFileChanged()));
    m_initialized = true;
}

void FcitxConnectionPrivate::finalize() {
    m_serviceWatcher->removeWatchedService(m_serviceName);
    m_watcher.data()->removePaths(m_watcher.data()->files());
    m_watcher.data()->removePaths(m_watcher.data()->directories());
    m_watcher.data()->disconnect(SIGNAL(fileChanged(QString)));
    m_watcher.data()->disconnect(SIGNAL(directoryChanged(QString)));
    m_initialized = false;
}

void FcitxConnectionPrivate::socketFileChanged() {
    if (m_watcher.isNull())
        return;

    QFileInfo info(socketFile());
    if (info.exists()) {
        if (m_watcher.data()->files().indexOf(info.filePath()) == -1)
            m_watcher.data()->addPath(info.filePath());
    }

    QString addr = address();
    if (addr.isNull())
        return;

    cleanUp();
    createConnection();
}

QByteArray FcitxConnectionPrivate::localMachineId()
{
#if QT_VERSION >= QT_VERSION_CHECK(4, 8, 0)
    return QDBusConnection::localMachineId();
#else
    QFile file1("/var/lib/dbus/machine-id");
    QFile file2("/etc/machine-id");
    QFile* fileToRead = NULL;
    if (file1.open(QIODevice::ReadOnly)) {
        fileToRead = &file1;
    }
    else if (file2.open(QIODevice::ReadOnly)) {
        fileToRead = &file2;
    }
    if (fileToRead) {
        QByteArray result = fileToRead->readLine(1024);
        fileToRead->close();
        result = result.trimmed();
        if (!result.isEmpty())
            return result;
    }
    return "machine-id";
#endif
}

int FcitxConnectionPrivate::displayNumber() {
    if (m_displayNumber >= 0)
        return m_displayNumber;
    Display * dpy = QX11Info::display();
    int displayNumber = 0;
    if (dpy) {
        char* display = XDisplayString(dpy);
        if (display) {
            char* strDisplayNumber = NULL;
            display = strdup(display);
            char* p = display;
            for (; *p != ':' && *p != '\0'; p++);

            if (*p == ':') {
                *p = '\0';
                p++;
                strDisplayNumber = p;
            }
            for (; *p != '.' && *p != '\0'; p++);

            if (*p == '.') {
                *p = '\0';
            }

            if (strDisplayNumber) {
                displayNumber = atoi(strDisplayNumber);
            }

            free(display);
        }
    }
    else
        displayNumber = fcitx_utils_get_display_number();

    m_displayNumber = displayNumber;

    return displayNumber;
}

const QString& FcitxConnectionPrivate::socketFile()
{
    if (!m_socketFile.isEmpty())
        return m_socketFile;

    char* addressFile = NULL;

    asprintf(&addressFile, "%s-%d", localMachineId().data(), displayNumber());

    char* file = NULL;

    FcitxXDGGetFileUserWithPrefix("dbus", addressFile, NULL, &file);

    QString path = QString::fromUtf8(file);
    free(file);
    free(addressFile);

    m_socketFile = path;

    return m_socketFile;
}

QString FcitxConnectionPrivate::address()
{
    QString addr;
    QByteArray addrVar = qgetenv("FCITX_DBUS_ADDRESS");
    if (!addrVar.isNull())
        return QString::fromLocal8Bit(addrVar);

    QFile file(socketFile());
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    const int BUFSIZE = 1024;

    char buffer[BUFSIZE];
    size_t sz = file.read(buffer, BUFSIZE);
    file.close();
    if (sz == 0)
        return QString();
    char* p = buffer;
    while(*p)
        p++;
    size_t addrlen = p - buffer;
    if (sz != addrlen + 2 * sizeof(pid_t) + 1)
        return QString();

    /* skip '\0' */
    p++;
    pid_t *ppid = (pid_t*) p;
    pid_t daemonpid = ppid[0];
    pid_t fcitxpid = ppid[1];

    if (!fcitx_utils_pid_exists(daemonpid)
        || !fcitx_utils_pid_exists(fcitxpid))
        return QString();

    addr = QLatin1String(buffer);

    return addr;
}

void FcitxConnectionPrivate::createConnection() {
    if (m_connectedOnce && !m_autoReconnect) {
        return;
    }

    m_serviceWatcher->disconnect(SIGNAL(serviceOwnerChanged(QString,QString,QString)));
    QString addr = address();
    // qDebug() << addr;
    if (!addr.isNull()) {
        QDBusConnection connection(QDBusConnection::connectToBus(addr, "fcitx"));
        if (connection.isConnected()) {
            // qDebug() << "create private";
            m_connection = new QDBusConnection(connection);
        }
        else
            QDBusConnection::disconnectFromBus("fcitx");
    }

    if (!m_connection) {
        QDBusConnection* connection = new QDBusConnection(QDBusConnection::sessionBus());
        connect(m_serviceWatcher, SIGNAL(serviceOwnerChanged(QString,QString,QString)), this, SLOT(imChanged(QString,QString,QString)));
        QDBusReply<bool> registered = connection->interface()->isServiceRegistered(m_serviceName);
        if (!registered.isValid() || !registered.value()) {
            delete connection;
        }
        else {
            m_connection = connection;
        }
    }

    Q_Q(FcitxConnection);
    if (m_connection) {

        m_connection->connect ("org.freedesktop.DBus.Local",
                            "/org/freedesktop/DBus/Local",
                            "org.freedesktop.DBus.Local",
                            "Disconnected",
                            this,
                            SLOT (dbusDisconnected ()));
        m_connectedOnce = true;
        emit q->connected();
    }
}


void FcitxConnectionPrivate::dbusDisconnected()
{
    cleanUp();

    createConnection();
}

void FcitxConnectionPrivate::imChanged(const QString& service, const QString& oldowner, const QString& newowner)
{
    if (service == m_serviceName) {
        /* old die */
        if (oldowner.length() > 0 || newowner.length() > 0)
            cleanUp();

        /* new rise */
        if (newowner.length() > 0) {
            QTimer::singleShot(100, this, SLOT(newServiceAppear()));
        }
    }
}

void FcitxConnectionPrivate::cleanUp()
{
    Q_Q(FcitxConnection);
    bool doemit = false;
    QDBusConnection::disconnectFromBus("fcitx");
    if (m_connection) {
        delete m_connection;
        m_connection = 0;
        doemit = true;
    }

    if (!m_autoReconnect && m_connectedOnce)
        finalize();

    /* we want m_connection and finalize being called before the signal
     * thus isConnected will return false in slot
     * and startConnection can be called in slot
     */
    if (doemit)
        emit q->disconnected();
}

bool FcitxConnectionPrivate::isConnected()
{
    return m_connection && m_connection->isConnected();
}

void FcitxConnectionPrivate::newServiceAppear() {
    if (!isConnected()) {
        cleanUp();

        createConnection();
    }
}