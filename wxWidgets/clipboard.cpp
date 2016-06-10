/////////////////////////////////////////////////////////////////////////////
// Name:   dcm.cpp
// Purpose: wxWidgets wrapper for dcm2niix
// Author:  Chris Rorden
// Copyright: (c) Chris Rorden
// Licence: BSD licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"
#include <wx/stdpaths.h>
#include "nii_dicom_batch.h"
#include "wx/clipbrd.h"
#include <iostream>

#ifdef __BORLANDC__
    #pragma hdrstop
#endif

// for all others, include the necessary headers (this file is usually all you
// need because it includes almost all "standard" wxWidgets headers)
#ifndef WX_PRECOMP
    #include "wx/wx.h"
#endif

#ifndef wxHAS_IMAGES_IN_RESOURCES
    #include "../sample.xpm"
#endif

class MyApp : public wxApp
{
public:
    virtual bool OnInit();
    
};

class MyFrame : public wxFrame
{
public:
    MyFrame(const wxString& title);
    void OnQuit(wxCommandEvent&event);
    void OnAbout(wxCommandEvent&event);
    void OnSetOutputFolder(wxCommandEvent&event);
    void OnDicom(wxCommandEvent&event);
    void OnParRec(wxCommandEvent&event);
    void OnCompressCheck(wxCommandEvent&event);
    void OnCopy(wxCommandEvent&event);
    void OnFileName(wxCommandEvent&event);
    void OnShowPrefs();
    void OnShowExampleFilename();
    void OnDropFiles(wxDropFilesEvent&event);
    void OnClose(wxCloseEvent& event);
    //void OnProcessFile: (char * fname);
    void OnProcessFile (char * fname);
private:
    wxTextCtrl        *m_textctrl;
    wxCheckBox		*m_compressCheck;
    wxTextCtrl        *m_fileName;
    wxButton		*m_outputFolder;
    struct TDCMopts opts;
    bool applicationBusy;
    DECLARE_EVENT_TABLE()
};

enum
{
    ID_Quit   = wxID_EXIT,
    ID_About  = wxID_ABOUT,
    ID_OutputFolder  = 100,
    ID_Text   = 101,
    ID_CompressCheck = 102,
    ID_DicomMenu = 103,
    ID_ParRecMenu = 104,
    ID_CopyMenu = 105,
    ID_FileName = 106
};

BEGIN_EVENT_TABLE(MyFrame, wxFrame)
    EVT_MENU(ID_Quit,  MyFrame::OnQuit)
    EVT_MENU(ID_About, MyFrame::OnAbout)
    EVT_MENU(ID_DicomMenu, MyFrame::OnDicom)
    EVT_MENU(ID_ParRecMenu, MyFrame::OnParRec)
    EVT_MENU(ID_CopyMenu, MyFrame::OnCopy)
    EVT_BUTTON(ID_OutputFolder, MyFrame::OnSetOutputFolder)
    EVT_CHECKBOX(ID_CompressCheck, MyFrame::OnCompressCheck)
    EVT_TEXT(ID_FileName, MyFrame::OnFileName)
    EVT_CLOSE(MyFrame::OnClose)
END_EVENT_TABLE()

IMPLEMENT_APP(MyApp)


void MyFrame::OnClose(wxCloseEvent& event) {
    saveIniFile (opts); //save preferences
    Destroy();
}

bool MyApp::OnInit() {
    if ( !wxApp::OnInit() )
        return false;
	MyFrame *frame = new MyFrame("dcm2");
    frame->SetClientSize(720, 440);
	frame->SetMinSize(wxSize(640, 340));    
    frame->Show(true);
    return true;
}

MyFrame::MyFrame(const wxString& title) : wxFrame(NULL, wxID_ANY, title) {
    // set the frame icon
    this->applicationBusy = true;
    SetIcon(wxICON(sample)); //&Open\tCtrl+O
    wxMenu *fileMenu = new wxMenu;
 	fileMenu->Append(ID_DicomMenu, wxT("&DICOM to NIfTI...\tCtrl-D"));
    fileMenu->Append(ID_ParRecMenu, wxT("&PAR/REC to NIfTI...\tCtrl-P"));
    fileMenu->Append(ID_Quit, "E&xit\tAlt-X", "Quit this program");
    wxMenu *editMenu = new wxMenu;
    editMenu->Append(ID_CopyMenu, "&Copy\tCtrl-C", "Copy text to clipboard");
    wxMenuBar *menuBar = new wxMenuBar();
    #ifdef __WXMAC__
		#define kTextLabelBorder 2
		editMenu->Append(ID_About, "&About", "Show about dialog");
	#else
		#define kTextLabelBorder 6
    	wxMenu *helpMenu = new wxMenu;
    	helpMenu->Append(ID_About, "&About", "Show about dialog");
    	menuBar->Append(helpMenu, "Help");
	#endif
	menuBar->Append(fileMenu, "File");
    menuBar->Append(editMenu, " Edit");
	SetMenuBar(menuBar);
    wxPanel *panel = new wxPanel( this, -1 );
    wxBoxSizer *main_sizer = new wxBoxSizer( wxVERTICAL );
    wxBoxSizer *hbox1 = new wxBoxSizer(wxHORIZONTAL);
	hbox1->Add( new wxStaticText( panel, wxID_ANY, "Compress" ), 0, wxEXPAND | wxTOP, kTextLabelBorder );
	m_compressCheck = new wxCheckBox( panel, ID_CompressCheck, "" );
	hbox1->Add( m_compressCheck, 0, wxBottom, 20);
    hbox1->Add( new wxStaticText( panel, wxID_ANY, " Output name" ), 0, wxEXPAND| wxTOP, kTextLabelBorder );
	m_fileName = new wxTextCtrl( panel, ID_FileName, "%f%s" );
	hbox1->Add( m_fileName, 1, wxEXPAND, 5 );
    hbox1->Add( new wxStaticText( panel, wxID_ANY, " Output folder" ), 0, wxEXPAND| wxTOP, kTextLabelBorder );
    m_outputFolder = new wxButton( panel, ID_OutputFolder, " Input folder" );
    hbox1->Add(m_outputFolder , 1, wxEXPAND, 0);
    main_sizer->Add(hbox1, 0, wxEXPAND| wxALL, 4);
    m_textctrl = new wxTextCtrl( panel, ID_Text, "", wxDefaultPosition,wxDefaultSize, wxTE_READONLY | wxTE_MULTILINE );
    main_sizer->Add( m_textctrl, 1, wxGROW );
    panel->SetSizer( main_sizer );
    wxString appPath = wxStandardPaths::Get().GetExecutablePath();
    const char *appPathC = appPath.mb_str(wxConvUTF8);
	readIniFile (&opts, &appPathC);
	//redirect http://docs.wxwidgets.org/trunk/classwx_text_ctrl.html
	//std::streambuf *sbOld = std::cout.rdbuf();
	std::cout.rdbuf(m_textctrl);
	//redirect until... std::cout.rdbuf(sbOld);
	m_textctrl->DragAcceptFiles(true);
	m_textctrl->Connect(wxEVT_DROP_FILES, wxDropFilesEventHandler(MyFrame::OnDropFiles), NULL, this);
	this->OnShowPrefs();
	
	this->applicationBusy = false;
}

void MyFrame::OnProcessFile (char * fname)
{
	m_textctrl->Clear();
    struct TDCMopts optsTemp;
    optsTemp = opts; //conversion may change values like the outdir (if not specified)
    strcpy(optsTemp.indir, fname);
	clock_t start = clock();
    nii_loadDir (&(optsTemp));
    //printf("required %fms\n", ((double)(clock()-start))/1000);
    std::cout << "required " <<((double)(clock()-start))/1000 <<"ms" <<std::endl; 
    fflush(stdout); //GUI buffers printf, display all results
}

void MyFrame::OnDropFiles(wxDropFilesEvent& event) {
	if (event.GetNumberOfFiles() < 1) return;
	wxString* dropped = event.GetFiles();
	wxString name =dropped[0];
	char fname[1024];
    strncpy(fname, (const char*)name.mb_str(wxConvUTF8), 1023);
    OnProcessFile (fname);	
}
    
void MyFrame::OnShowExampleFilename() {
    char niiFilename[1024];
    nii_createDummyFilename(niiFilename, opts);
    m_textctrl->Clear();
    wxString mystring = wxString::Format(wxT("%s\nVersion %s\n"),niiFilename,kDCMvers);
    m_textctrl->AppendText(mystring);
}

void MyFrame::OnShowPrefs()
{
   applicationBusy = true;
   m_compressCheck->SetValue(opts.isGz) ;
   m_fileName->Clear();
   m_fileName->AppendText(opts.filename);
   //char cstring[1024];
   //strcpy(cstring,opts.outdir);
   //wxString outdir = wxString::FromUTF8(cstring);
   wxString outdir = wxString::FromUTF8(opts.outdir);
   if (outdir.length() < 1) 
   	m_outputFolder->SetLabel("Input folder");
   else { 
   		if (outdir.Length() > 40) {
   			outdir.Truncate(40);
   			outdir.Append("...");
   		}
   		m_outputFolder->SetLabel(outdir);;
   }
   this->OnShowExampleFilename();
   applicationBusy = false;
}

void MyFrame::OnFileName(wxCommandEvent& WXUNUSED(event))
{
	//only respond to inputs from the user, http://wxwidgets.10942.n7.nabble.com/wxTextCtrl-constructor-generates-EVT-TEXT-td24562.html
	if (applicationBusy) return;
	strncpy(opts.filename, (const char*)m_fileName->GetValue().mb_str(wxConvUTF8), 1023);
	this->OnShowExampleFilename();
}

void MyFrame::OnCompressCheck(wxCommandEvent& WXUNUSED(event))
{
	opts.isGz = m_compressCheck->GetValue();
	this->OnShowExampleFilename();
}

void MyFrame::OnParRec(wxCommandEvent& WXUNUSED(event))
{
   wxFileDialog* fileDialog = new wxFileDialog(
		this, _("Choose a file to covert"), wxEmptyString, wxEmptyString, 
		_("PAR/REC Image (*.par)||*.par;*.PAR"),
		wxFD_OPEN, wxDefaultPosition); 
   if (fileDialog->ShowModal() == wxID_OK) {
		//wxString file = fileDialog->GetPath();
   		//wxMessageBox(file);
   		char fname[1024];
    	strncpy(fname, (const char*)fileDialog->GetPath().mb_str(wxConvUTF8), 1023);
    	OnProcessFile (fname);
   }
   fileDialog->Destroy();
}

void MyFrame::OnDicom(wxCommandEvent& WXUNUSED(event))
{
   wxString pth = wxT("");
   wxDirDialog dirDialog(this, wxT("Select the DICOM folder"),pth, wxDD_NEW_DIR_BUTTON);
   if (dirDialog.ShowModal() != wxID_OK) return;
   //wxString dir = dirDialog.GetPath();
   char fname[1024];
   strncpy(fname, (const char*)dirDialog.GetPath().mb_str(wxConvUTF8), 1023);
   OnProcessFile (fname);
   //wxMessageBox(dir);
}

void MyFrame::OnCopy(wxCommandEvent& WXUNUSED(event))
{
	m_textctrl->SelectAll();
	m_textctrl->Copy();
	m_textctrl->SetSelection(m_textctrl->XYToPosition(0,0),m_textctrl->XYToPosition(0,0));
}

void MyFrame::OnSetOutputFolder(wxCommandEvent& WXUNUSED(event))
{
   wxString pth = wxT("");
   wxDirDialog dialog(this, wxT("Select out folder (or cancel to use input folder)"),pth, wxDD_NEW_DIR_BUTTON);
   if (dialog.ShowModal() != wxID_OK) return;
   wxString dir = dialog.GetPath();
   strncpy(opts.outdir, (const char*)dialog.GetPath().mb_str(wxConvUTF8), 1023);
   this->OnShowPrefs();
}

void MyFrame::OnQuit(wxCommandEvent& WXUNUSED(event))
{
    Close(true);
}

void MyFrame::OnAbout(wxCommandEvent& WXUNUSED(event))
{
    wxMessageBox("Chris Rorden :: www.mricro.com","dcm2nii",  wxOK, this);
}


