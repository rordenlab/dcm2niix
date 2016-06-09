unit form;

 {$IFDEF FPC} {$mode delphi}{$H+} {$ENDIF}
interface

uses
  {$IFNDEF UNIX} Registry, {$ENDIF}
  Classes, SysUtils, FileUtil, Forms, Controls, Graphics, Dialogs, ExtCtrls,
  StdCtrls, Menus, Process, untar;

type

  { TForm1 }
  TForm1 = class(TForm)
    MainMenu1: TMainMenu;
    FileMenu: TMenuItem;
    EditMenu: TMenuItem;
    CopyMenu: TMenuItem;
    DicomMenu: TMenuItem;
    ResetMenu: TMenuItem;
    OpenDialog1: TOpenDialog;
    ParRecMenu: TMenuItem;
    outputFolderName: TButton;
    compressCheck: TCheckBox;
    Label2: TLabel;
    outputFolderLabel: TLabel;
    outnameEdit: TEdit;
    outnameLabel: TLabel;
    Memo1: TMemo;
    Panel1: TPanel;
    procedure compressCheckClick(Sender: TObject);
    procedure DicomMenuClick(Sender: TObject);
    procedure FormResize(Sender: TObject);
    function getOutputFolder: string;

    procedure outnameEditKeyUp(Sender: TObject; var Key: Word;
      Shift: TShiftState);
    procedure ParRecMenuClick(Sender: TObject);
    procedure ProcessFile(infilename: string);
    procedure FormClose(Sender: TObject; var CloseAction: TCloseAction);
    procedure FormCreate(Sender: TObject);
    procedure FormDropFiles(Sender: TObject; const FileNames: array of String);
    procedure CopyMenuClick(Sender: TObject);
    procedure outputFolderNameClick(Sender: TObject);
    procedure ResetMenuClick(Sender: TObject);
    procedure RunCmd (lCmd: string);
    function getExeName : string; //return path for command line tool
    procedure readIni (ForceReset: boolean); //load preferences
    procedure writeIni; //save preferences
  private
    { private declarations }
  public
    { public declarations }
  end;

var
  Form1: TForm1;

implementation
{$R *.lfm}

const kExeName = 'dcm2niix' ;
  var
    isAppDoneInitializing : boolean = false;

function FindDefaultExecutablePathX(const Executable: string): string;
begin
     result := FindDefaultExecutablePath(kExeName);
     if result = '' then
        result := FindDefaultExecutablePath(ExtractFilePath  (paramstr(0)) +kExeName);
end;

function TForm1.getExeName : string;
var
  lF: string;
begin
     result := FindDefaultExecutablePathX(kExeName);
     if not fileexists(result) then begin
        lF :=  ExtractFilePath (paramstr(0));
        result := lF+kExeName;
        if not fileexists(result) then begin
           Memo1.Lines.Clear;
           memo1.Lines.Add('Error: unable to find executable '+kExeName+' in path');
           memo1.Lines.Add(' Solution: copy '+kExeName+' to '+lF);
           result := '';
        end;  //not in same folder as GUI
     end; //not in path
     {$IFNDEF UNIX} //strip .exe for Windows
     result := ChangeFileExt(result, '');
     {$ENDIF}
end; //exeName()

{$IFDEF UNIX}
function iniName : string;
begin
     result := GetEnvironmentVariable ('HOME')+PathDelim+'.dcm2nii.ini';
end;

procedure TForm1.writeIni;
var
   iniFile : TextFile;
 begin
   AssignFile(iniFile, iniName);
   ReWrite(iniFile);
   if (compressCheck.checked) then
      WriteLn(iniFile, 'isGZ=1')
   else
       WriteLn(iniFile, 'isGZ=0');
   WriteLn(iniFile, 'filename='+outnameEdit.caption);
   CloseFile(iniFile);
end; //writeIni

procedure TForm1.readIni (ForceReset: boolean);
var
  fileData, rowData : TStringList;
  row, i: integer;
  opts_isGz: boolean;
  opts_filename: string;
begin
     opts_isGz := true;
     //opts_outdir := '';
     opts_filename := '%t_%p_%s';
     if FileExists( iniName) and (not (ForceReset )) then begin
        fileData := TStringList.Create;
        fileData.LoadFromFile(iniName);  // Load from Testing.txt file
        if (fileData.Count > 0) then begin
           rowData := TStringList.Create;
           rowData.Delimiter := '=';
           for row := 0 to (fileData.Count-1) do begin //for each row of file
               rowData.DelimitedText:=fileData[row];
               if ((rowData.Count > 1) and (CompareText(rowData[0] ,'isGZ')= 0)) then
                  opts_isGz := (CompareText(rowData[1],'1') = 0);
               if ((rowData.Count > 1) and (CompareText(rowData[0] ,'filename')= 0)) then begin
                  opts_filename := '';
                  if (rowData.Count > 2) then
                     for i := 1 to (rowData.Count-2) do
                         opts_filename := opts_filename+ rowData[i]+' ';
                  opts_filename := opts_filename+ rowData[rowData.Count-1];
               end;
           end;
          rowData.Free;
        end;
        fileData.Free;
     end else
         memo1.Lines.Add('Using default settings');
     compressCheck.Checked := opts_isGz;
     outnameEdit.Caption := opts_filename;
     getExeName;
end; //readIni()
{$ELSE}
//For Windows we save preferences in the registry to ensure user has write access
procedure TForm1.writeIni;
var
  ARegistry: TRegistry;
begin
     ARegistry := TRegistry.Create;
     ARegistry.RootKey := HKEY_CURRENT_USER;//HKEY_LOCAL_MACHINE;
     if ARegistry.OpenKey ('\Software\dcm2nii',true) then begin
       	  ARegistry.WriteBool('isGZ', compressCheck.Checked );
       	  ARegistry.WriteString('filename', outnameEdit.Caption );
     end;
     ARegistry.Free;
end; //writeIni()

procedure TForm1.readIni (ForceReset: boolean);
var
  ARegistry: TRegistry;
  opts_isGz: boolean;
  opts_filename: string;
begin
     //showmessage(inttostr(SizeOf(Integer)));
     opts_isGz := true;
     opts_filename := '%t_%p_%s';
     if not ForceReset then begin
       ARegistry := TRegistry.Create;
       ARegistry.RootKey := HKEY_CURRENT_USER;//HKEY_LOCAL_MACHINE;
       if ARegistry.OpenKey ('\Software\dcm2nii',true) then begin
       	    if ARegistry.ValueExists( 'isGZ' ) then
          	   opts_isGz := ARegistry.ReadBool( 'isGZ' );
       	    if ARegistry.ValueExists( 'isGZ' ) then
          	   opts_filename := ARegistry.ReadString( 'filename' );
       end;
       ARegistry.Free;
     end;
     compressCheck.Checked := opts_isGz;
     outnameEdit.Caption := opts_filename;
     getExeName;
end; //readIni()
{$ENDIF}

procedure TForm1.RunCmd (lCmd: string);
//http://wiki.freepascal.org/Executing_External_Programs
var
  OutputLines: TStringList;
  MemStream: TMemoryStream;
  OurProcess: TProcess;
  NumBytes: LongInt;
  BytesRead: LongInt;
const
  READ_BYTES = 2048;
begin
   if (not isAppDoneInitializing) then exit;
   if (getExeName = '') then exit;
   Memo1.Lines.Clear;
   Memo1.Lines.Add(lCmd);
   Form1.refresh; Memo1.refresh; Memo1.invalidate;
   MemStream := TMemoryStream.Create;
   BytesRead := 0;
   OurProcess := TProcess.Create(nil);
   {$IFDEF UNIX}
   OurProcess.Environment.Add(GetEnvironmentVariable('PATH'));
   {$ENDIF}
   OurProcess.CommandLine := lCmd;
  // We cannot use poWaitOnExit here since we don't
  // know the size of the output. On Linux the size of the
  // output pipe is 2 kB; if the output data is more, we
  // need to read the data. This isn't possible since we are
  // waiting. So we get a deadlock here if we use poWaitOnExit.
  OurProcess.Options := [poUsePipes, poNoConsole];
  OurProcess.Execute;
  while True do begin
    // make sure we have room
    MemStream.SetSize(BytesRead + READ_BYTES);
    // try reading it
    NumBytes := OurProcess.Output.Read((MemStream.Memory + BytesRead)^, READ_BYTES);
    if NumBytes > 0 // All read() calls will block, except the final one.
    then begin
      Inc(BytesRead, NumBytes);
    end else
      BREAK // Program has finished execution.
  end;
  MemStream.SetSize(BytesRead);
  OutputLines := TStringList.Create;
  OutputLines.LoadFromStream(MemStream);
  Memo1.Lines.AddStrings(OutputLines);
  OutputLines.Free;
  OurProcess.Free;
  MemStream.Free;
end;

function TForm1.getOutputFolder: string;
begin
     if (outputFolderName.Tag > 0) then
        result := outputFolderName.Caption
     else
         result := '';
end; //getOutputFolder

procedure TForm1.ProcessFile(infilename: string);
var
  cmd, outputFolder, inFolder: string;
begin
  inFolder := infilename;
  if isTGZ(inFolder) then begin
  	 infolder := deTGZ(infolder);
     if infolder = '' then exit; //error
 end;
 cmd := getExeName +' ';
 if compressCheck.checked then
    cmd := cmd + '-z y '
 else
     cmd := cmd + '-z n ';
 outputFolder := getOutputFolder;
 if length(outputFolder) > 0 then
     cmd := cmd + '-o '+outputFolder+' ';
 cmd := cmd + '-f "'+outnameEdit.Text+'" ';
 if length(inFolder) > 0 then
     cmd := cmd +'"'+inFolder+'"';
 RunCmd(cmd);
end; //ProcessFile()

procedure TForm1.outnameEditKeyUp(Sender: TObject; var Key: Word;
  Shift: TShiftState);
begin
      ProcessFile('');
end; //outnameEditKeyUp()

procedure TForm1.ParRecMenuClick(Sender: TObject);
var
  lI: integer;
begin
  if not OpenDialog1.execute then exit;
  //ProcessFile(OpenDialog1.filename);
  if OpenDialog1.Files.count < 1 then exit;
     for lI := 0 to (OpenDialog1.Files.count-1) do
         ProcessFile(OpenDialog1.Files[lI]);
end; //ParRecMenuClick()

function getDirPrompt (lDefault: string): string;
begin
  result := lDefault;  // Set the starting directory
  chdir(result); //start search from default dir...
  if SelectDirectory(result, [sdAllowCreate,sdPerformCreate,sdPrompt], 0) then
     chdir(result)
  else
      result := '';
end;  //getDirPrompt()

procedure TForm1.DicomMenuClick(Sender: TObject);
var
  dir: string;
begin
     dir := getDirPrompt('');
     ProcessFile( dir);
end; //DicomMenuClick()

procedure TForm1.compressCheckClick(Sender: TObject);
begin
  ProcessFile('');
end;

procedure TForm1.FormResize(Sender: TObject);
begin
  outputFolderName.width := Form1.Width-outputFolderName.left-2;
end; //FormResize()

procedure TForm1.FormDropFiles(Sender: TObject; const FileNames: array of String);
begin
    ProcessFile( FileNames[0]);
end; //FormDropFiles()

procedure TForm1.CopyMenuClick(Sender: TObject);
begin
     Memo1.SelectAll;
     Memo1.CopyToClipboard;
end; //CopyMenuClick()

procedure TForm1.outputFolderNameClick(Sender: TObject);
var
  lDir : string;
begin
     if (outputFolderName.Tag > 0) then //start search from prior location
        lDir := outputFolderName.Caption
     else
         lDir := '';
     lDir := getDirPrompt(lDir);
     outputFolderName.Tag := length(lDir);
     if length(lDir) > 0 then
        outputFolderName.Caption := lDir
     else
         outputFolderName.Caption := 'input folder';
end; //outputFolderNameClick()

procedure TForm1.ResetMenuClick(Sender: TObject);
begin
  isAppDoneInitializing := false;
     readIni(true);
     isAppDoneInitializing := true;
     ProcessFile('');
end;

procedure TForm1.FormCreate(Sender: TObject);
begin
     readIni(false);
     //memo1.lines.add('for details see http://www.mccauslandcenter.sc.edu/CRNL/tools/dcm2niix');
     application.ShowButtonGlyphs:= sbgNever;
     isAppDoneInitializing := true;
     ProcessFile('');
end; //FormCreate()

procedure TForm1.FormClose(Sender: TObject; var CloseAction: TCloseAction);
begin
     writeIni;
end; //FormClose()

end.

