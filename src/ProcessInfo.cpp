/*
    Copyright (C) 2007 by Robert Knight <robertknight@gmail.countm>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301  USA.
*/

// Qt
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QRegExp>
#include <QTextStream>
#include <QStringList>

// Konsole
#include "ProcessInfo.h"

using namespace Konsole;

ProcessInfo::ProcessInfo(int pid , bool enableEnvironmentRead)
    : _fields( ARGUMENTS | ENVIRONMENT ) // arguments and environments
                                         // are currently always valid,
                                         // they just return an empty
                                         // vector / map respectively
                                         // if no arguments
                                         // or environment bindings
                                         // have been explicitly set
    , _enableEnvironmentRead(enableEnvironmentRead)
    , _pid(pid)
    , _parentPid(0)
    , _foregroundPid(0)
{
}

void ProcessInfo::update() 
{
    readProcessInfo(_pid,_enableEnvironmentRead);
}

QString ProcessInfo::format(const QString& input) const
{
   bool ok = false;

   QString output(input);

   // search for and replace known markers
   output.replace("%u","NOT IMPLEMENTED YET");
   output.replace("%n",name(&ok));
   output.replace("%D",currentDir(&ok));
   output.replace("%d",QFileInfo(currentDir(&ok)).baseName());
   
   // remove any remaining %[LETTER] sequences
   output.replace(QRegExp("%\\w"),QString::null);

   return output;
}

QVector<QString> ProcessInfo::arguments(bool* ok) const
{
    *ok = _fields & ARGUMENTS;

    return _arguments;
}

QMap<QString,QString> ProcessInfo::environment(bool* ok) const
{
    *ok = _fields & ENVIRONMENT;

    return _environment;
}

bool ProcessInfo::isValid() const
{
    return _fields & PROCESS_ID;
}

int ProcessInfo::pid(bool* ok) const
{
    *ok = _fields & PROCESS_ID;

    return _pid;
}

int ProcessInfo::parentPid(bool* ok) const
{
    *ok = _fields & PARENT_PID;

    return _parentPid;
}

int ProcessInfo::foregroundPid(bool* ok) const
{
    *ok = _fields & FOREGROUND_PID;

    return _foregroundPid;
}

QString ProcessInfo::name(bool* ok) const
{
    *ok = _fields & NAME;

    return _name;
}

void ProcessInfo::setPid(int pid)
{
    _pid = pid;
    _fields |= PROCESS_ID;
}

void ProcessInfo::setParentPid(int pid)
{
    _parentPid = pid;
    _fields |= PARENT_PID;
}
void ProcessInfo::setForegroundPid(int pid)
{
    _foregroundPid = pid;
    _fields |= FOREGROUND_PID;
}
QString ProcessInfo::currentDir(bool* ok) const
{
    *ok = _fields & CURRENT_DIR;

    return _currentDir;
}
void ProcessInfo::setCurrentDir(const QString& dir)
{
    _fields |= CURRENT_DIR;
    _currentDir = dir;
}

void ProcessInfo::setName(const QString& name)
{
    _name = name;
    _fields |= NAME;
}
void ProcessInfo::addArgument(const QString& argument)
{
    _arguments << argument;    
}

void ProcessInfo::addEnvironmentBinding(const QString& name , const QString& value)
{
    _environment.insert(name,value);
}

UnixProcessInfo::UnixProcessInfo(int pid,bool enableEnvironmentRead)
    : ProcessInfo(pid,enableEnvironmentRead)
{
}

bool UnixProcessInfo::readProcessInfo(int pid , bool enableEnvironmentRead)
{
    // indicies of various fields within the process status file which
    // contain various information about the process
    const int PARENT_PID_FIELD = 3;
    const int PROCESS_NAME_FIELD = 1;
    const int GROUP_PROCESS_FIELD = 7;
    
    QString parentPidString;
    QString processNameString;
    QString foregroundPidString;

    // read process status file ( /proc/<pid/stat )
    //
    // the expected file format is a list of fields separated by spaces, using
    // parenthesies to escape fields such as the process name which may itself contain
    // spaces:
    //
    // FIELD FIELD (FIELD WITH SPACES) FIELD FIELD
    //
    QFile processInfo( QString("/proc/%1/stat").arg(pid) );
    if ( processInfo.open(QIODevice::ReadOnly) )
    {
        QTextStream stream(&processInfo);
        QString data = stream.readAll();

        int stack = 0;
        int field = 0;
        int pos = 0;

        while (pos < data.count())
        {
            QChar c = data[pos];

            if ( c == '(' )
                stack++;
            else if ( c == ')' )
                stack--;
            else if ( stack == 0 && c == ' ' )
                field++;
            else
            {
                switch ( field )
                {
                    case PARENT_PID_FIELD:
                        parentPidString.append(c);
                        break;
                    case PROCESS_NAME_FIELD:
                        processNameString.append(c);
                        break;
                    case GROUP_PROCESS_FIELD:
                        foregroundPidString.append(c);
                        break;
                }
            }

            pos++; 
        }
    }
    else
    {
        qDebug() << __FUNCTION__ << "failed to open stat file";
        return false;
    }
    
    // check that data was read successfully
    bool ok = false;
    int foregroundPid = foregroundPidString.toInt(&ok);    
    if (!ok) return false;

    int parentPid = parentPidString.toInt(&ok);
    if (!ok) return false;

    if (processNameString.isEmpty()) return false;

    if (!readArguments(pid)) return false;
   
    if (!readCurrentDir(pid)) return false;

    if ( enableEnvironmentRead )
    {
        if (!readEnvironment(pid)) return false;
    }

    // update object state
    setPid(pid);
    setName(processNameString);
    setForegroundPid(foregroundPid);
    setParentPid(parentPid);

    return true;
}

ProcessInfo* ProcessInfo::newInstance(int pid,bool enableEnvironmentRead)
{
#ifdef Q_OS_UNIX
    return new UnixProcessInfo(pid,enableEnvironmentRead);
#else
    return new NullProcessInfo(pid,enableEnvironmentRead);
#endif
}

NullProcessInfo::NullProcessInfo(int pid,bool enableEnvironmentRead)
    : ProcessInfo(pid,enableEnvironmentRead)
{
}

bool NullProcessInfo::readProcessInfo(int /*pid*/ , bool /*enableEnvironmentRead*/)
{
    return false;
}

bool UnixProcessInfo::readArguments(int pid)
{
    // read command-line arguments file found at /proc/<pid>/cmdline
    // the expected format is a list of strings delimited by null characters,
    // and ending in a double null character pair.

    QFile argumentsFile( QString("/proc/%1/cmdline").arg(pid) );
    if ( argumentsFile.open(QIODevice::ReadOnly) )
    {
        QTextStream stream(&argumentsFile);
        QString data = stream.readAll();

        QStringList argList = data.split( QChar('\0') );
        
        foreach ( QString entry , argList )
        {
            if (!entry.isEmpty())
                addArgument(entry);
        }    
    }

    return true;
}

bool UnixProcessInfo::readCurrentDir(int pid)
{
    QFileInfo info( QString("/proc/%1/cwd").arg(pid) );
    if ( info.isSymLink() )
    {
        setCurrentDir( info.symLinkTarget() );
        return true;
    }
    else
    {
        return false;
    }
}

bool UnixProcessInfo::readEnvironment(int pid)
{
    // read environment bindings file found at /proc/<pid>/environ
    // the expected format is a list of KEY=VALUE strings delimited by null
    // characters and ending in a double null character pair.

    QFile environmentFile( QString("/proc/%1/environ").arg(pid) );
    if ( environmentFile.open(QIODevice::ReadOnly) )
    {
        QTextStream stream(&environmentFile);
        QString data = stream.readAll();

        QStringList bindingList = data.split( QChar('\0') );
   
        foreach( QString entry , bindingList )
        {
            QString name;
            QString value;

            int splitPos = entry.indexOf('=');

            if ( splitPos != -1 )
            {
                name = entry.mid(0,splitPos);
                value = entry.mid(splitPos+1,-1);

                addEnvironmentBinding(name,value);  
            }  
        }
    }

    return true;
}

SSHProcessInfo::SSHProcessInfo(const ProcessInfo& process)
 : _process(process)
{
    bool ok = false;

    // check that this is a SSH process
    const QString& name = _process.name(&ok);

    if ( !ok || name != "ssh" )
    {
        if ( !ok )
            qDebug() << "Could not read process info";
        else
            qDebug() << "Process is not a SSH process";

        return;
    }

    // read arguments
    const QVector<QString>& args = _process.arguments(&ok); 

    // SSH options
    // these are taken from the SSH manual ( accessed via 'man ssh' )
    
    // options which take no arguments
    static const QString noOptionsArguments("1246AaCfgkMNnqsTtVvXxY"); 
    // options which take one argument
    static const QString singleOptionArguments("bcDeFiLlmOopRSw");

    if ( ok )
    {
         // find the username, host and command arguments
         //
         // the username/host is assumed to be the first argument 
         // which is not an option
         // ( ie. does not start with a dash '-' character )
         // or an argument to a previous option.
         //
         // the command, if specified, is assumed to be the argument following
         // the username and host
         //
         // note that we skip the argument at index 0 because that is the
         // program name ( expected to be 'ssh' in this case )
         for ( int i = 1 ; i < args.count() ; i++ )
         {
            // if this argument is an option then skip it, plus any 
            // following arguments which refer to this option
            if ( args[i].startsWith('-') )
            {
                QChar argChar = ( args[i].length() > 1 ) ? args[i][1] : '\0';

                if ( noOptionsArguments.contains(argChar) )
                    continue;
                else if ( singleOptionArguments.contains(argChar) )
                {
                    i++;
                    continue;
                }
            }

            // check whether the host has been found yet
            // if not, this must be the username/host argument 
            if ( _host.isEmpty() )
            {
                // found username and host argument
                qDebug() << "[username] and host: " << args[i];

                // check to see if only a hostname is specified, or whether
                // both a username and host are specified ( in which case they
                // are separated by an '@' character:  username@host )
            
                int separatorPosition = args[i].indexOf('@');
                if ( separatorPosition != -1 )
                {
                    // username and host specified
                    _user = args[i].left(separatorPosition);
                    _host = args[i].mid(separatorPosition+1);

                    qDebug() << "found user: " << _user;
                    qDebug() << "found host: " << _host;
                }
                else
                {
                    // just the host specified
                    _host = args[i];
                    qDebug() << "found only host: " << _host;
                }
            }
            else
            {
                // host has already been found, this must be the command argument
                _command = args[i];

                qDebug() << "found command: " << _command;
            }

         }
    }
    else
    {
        qDebug() << "Could not read arguments";
        
        return;
    }
}

QString SSHProcessInfo::userName() const
{
    return _user;
}
QString SSHProcessInfo::host() const
{
    return _host;
}
QString SSHProcessInfo::command() const
{
    return _command;
}
QString SSHProcessInfo::format(const QString& input) const
{
    QString output(input);
    
    // search for and replace known markers
    output.replace("%u",_user);
    output.replace("%h",_host.left(_host.indexOf('.')));
    output.replace("%H",_host);
    output.replace("%c",_command);

    // remove any remaining %[LETTER] character sequences
    output.replace(QRegExp("%\\w"),QString::null);

    return output;
}



