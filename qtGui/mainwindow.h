#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QDir>
#include <QDropEvent>
#include <QUrl>
#include <QDebug>
#include <QMimeData>
//#include <QProcess>
#include "nii_dicom_batch.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();
    //QProcess *myProcess;
    //Ui::MainWindow *ui;
        struct TDCMopts opts;
private slots:
    //void on_pushButton_clicked();

    void on_compressCheck_clicked();

    void on_actionCopy_triggered();

    void on_actionPAR_REC_to_NIfTI_triggered();

    void on_actionDICOM_to_NIfTI_triggered();


    //void on_lineEdit_textEdited(const QString &arg1);
    //void updateText();
    //void readData();
void processFile(const QString &arg1);


    void on_folderButton_clicked();

    void on_outputFilenameEdit_textEdited(const QString &arg1);

private:
    Ui::MainWindow *ui;
    QDir directory;
protected:
    void showExampleFilename(bool doClear) ;
    void showPrefs(bool doClear);
    void dropEvent(QDropEvent *ev);
    void dragEnterEvent(QDragEnterEvent *ev);
    //StdoutRedirector *redirector;
};

#endif // MAINWINDOW_H
