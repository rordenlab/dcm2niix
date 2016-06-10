//http://www.qtforum.org/article/678/redirecting-cout-cerr-to-qdebug.html
#ifndef Q_DEBUGSTREAM_H
#define Q_DEBUGSTREAM_H

#include <iostream>
#include <streambuf>
#include <string>

#include "QTextEdit.h"

class Q_DebugStream : public std::basic_streambuf<char>
{
public:
    Q_DebugStream(std::ostream &stream, QTextEdit* text_edit) : m_stream(stream)
    {
        log_window = text_edit;
        m_old_buf = stream.rdbuf();
        stream.rdbuf(this);
    }

    ~Q_DebugStream()
    {
        m_stream.rdbuf(m_old_buf);
    }

    static void registerQDebugMessageHandler(){
        qInstallMessageHandler(myQDebugMessageHandler);
    }

private:

    static void myQDebugMessageHandler(QtMsgType, const QMessageLogContext &, const QString &msg)
    {
        std::cout << msg.toStdString().c_str();
    }

protected:

    //This is called when a std::endl has been inserted into the stream
    virtual int_type overflow(int_type v)
    {
        if (v == '\n') {
            log_window->append("");
        }
        return v;
    }

    /*virtual std::streamsize xsputn(const char *p, std::streamsize n)
    {
        std::string m_string;
     m_string.append(p, p + n);

     int pos = 0;
     while (pos != std::string::npos)
     {
      pos = m_string.find('\n');
      if (pos != std::string::npos)
      {
       std::string tmp(m_string.begin(), m_string.begin() + pos);
       log_window->append(tmp.c_str());
       m_string.erase(m_string.begin(), m_string.begin() + pos + 1);
      }
     }
     return n;
    }*/

    virtual std::streamsize xsputn(const char *p, std::streamsize n)
    {
    QString str(p);
    //str.remove(QRegExp("[^a-zA-Z/0-9. \\d\\s]"));
    //QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    //str.remove(QRegExp(QString::fromUtf8("[-`~!@#$%^&*()_—+=|:;<>«»,.?/{}\'\"\\\[\\\]\\\\]")));
    if (str.contains("\n")){
            QStringList strSplitted = str.split("\n");

            log_window->moveCursor (QTextCursor::End);
            log_window->insertPlainText (strSplitted.at(0)); //Index 0 is still on the same old line

            for(int i = 1; i < strSplitted.size(); i++){
                log_window->append(strSplitted.at(i));
            }
        }else{
            log_window->moveCursor (QTextCursor::End);
            log_window->insertPlainText (str);
            //log_window->append(str);
        }
        return n;
    }

private:
    std::ostream &m_stream;
    std::streambuf *m_old_buf;
    QTextEdit* log_window;
};


#endif // Q_DEBUGSTREAM_H
