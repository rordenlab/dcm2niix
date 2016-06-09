#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QDragEnterEvent>
#include <QClipboard>
#include "nii_dicom_batch.h"
#include "Q_DebugStream.h"
#include <time.h>  // clock_t, clock, CLOCKS_PER_SEC
#if defined(_WIN64) || defined(_WIN32)
#include <windows.h> //write to registry
#endif

void MainWindow::showPrefs(bool doClear) {
    ui->compressCheck->setChecked(opts.isGz);
    ui->outputFilenameEdit->setText(opts.filename);
    size_t kLen = 40;
    if (strlen(opts.outdir) < 1)
        ui->folderButton->setText("input folder");
    else if (strlen(opts.outdir) > kLen) {
        char cString[kLen+1];
        memcpy(cString,opts.outdir,kLen-1);
        cString[kLen-1] = 0;
        ui->folderButton->setText(cString);
    } else
        ui->folderButton->setText(opts.outdir);
    this->showExampleFilename( doClear);
}

void MainWindow::processFile(const QString &arg1) {
    //convert folder (DICOM) or file (PAR/REC) to NIFTI format
    ui->theTextView->clear();
    struct TDCMopts optsTemp;
    optsTemp = opts; //conversion may change values like the outdir (if not specified)
    strcpy(optsTemp.indir, arg1.toStdString().c_str());
    printf("processing %s\n", arg1.toStdString().c_str());
    clock_t start = clock();
    nii_loadDir (&(optsTemp));
    std::cout<< "Required "<<((float)(clock()-start))/CLOCKS_PER_SEC<<"seconds."<<std::endl;
}

void MainWindow::showExampleFilename(bool doClear) {
    char niiFilename[1024];
    nii_createDummyFilename(niiFilename, opts);
    if (doClear) ui->theTextView->clear();
    std::cout<<niiFilename<<std::endl;
    std::cout<< "Version "<<kDCMvers<<std::endl;
}

void MainWindow::on_outputFilenameEdit_textEdited(const QString &arg1)
{
    strcpy(opts.filename, arg1.toStdString().c_str());
    this->showExampleFilename(true);
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->theTextView->setReadOnly(true);
    setAcceptDrops(true);
    new QDebugStream(std::cout, ui->theTextView); //Redirect Console output to QTextEdit
    //new Q_DebugStream(std::cout, ui->theTextView); //Redirect Console output to QTextEdit
    //Q_DebugStream::registerQDebugMessageHandler(); //Redirect qDebug() output to QTextEdit
    const char *appPath =QCoreApplication::applicationFilePath().toStdString().c_str();
    readIniFile (&opts, &appPath);
    this->showPrefs(false);
}

MainWindow::~MainWindow()
{
    saveIniFile (opts); //save preferences
    delete ui;
}

void MainWindow::on_compressCheck_clicked()
{
    opts.isGz = ui->compressCheck->isChecked();
    this->showExampleFilename(true);
}

void MainWindow::on_actionCopy_triggered()
{
    QString newText = ui->theTextView->toPlainText();
    QApplication::clipboard()->setText(newText);
}

void MainWindow::on_actionPAR_REC_to_NIfTI_triggered()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Philips PAR image"),directory.path(),
        tr("PAR Files (*.par *.PAR)"));
    if ( fileName.isNull()  ) return;
    processFile(fileName);
}

void MainWindow::on_actionDICOM_to_NIfTI_triggered()
{
    QString path = QFileDialog::getExistingDirectory (this, tr("Directory"), directory.path());
    if ( path.isNull() ) return;
    directory.setPath(path);
    processFile(path);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *ev)
{
    ev->accept();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    QList<QUrl> urls = event->mimeData()->urls();
    event->acceptProposedAction();
    processFile(urls[0].toLocalFile());
}

void MainWindow::on_folderButton_clicked()
{
    QString path = QFileDialog::getExistingDirectory (this, tr("Select output folder (cancel to use input folder)"), directory.path());
    if ( path.isNull() )  path = "";
    strcpy(opts.outdir, path.toStdString().c_str());
    directory.setPath(path);
    this->showPrefs(true);
}
