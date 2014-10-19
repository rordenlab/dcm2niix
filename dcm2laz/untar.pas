unit untar;

interface
{$IFDEF FPC}{$mode delphi}{$H+}{$ENDIF}
uses

sysutils, LibTar, classes, dialogs, zstream;

function deTGZ (lFilename: string): string;
function isTGZ (var lStr: string): boolean;

implementation

function isTGZ (var lStr: string): boolean;
var lExt: string;
begin
   lExt := ExtractFileExt(lStr);
   lExt := UpperCase(lExt);
   if (lExt='.TGZ') then
      Result := true
   else
       Result := false;
end;

procedure Extract (var lTarFile: string; lOverwrite: boolean); //extract target
VAR
  TA        : TTarArchive;
  DirRec    : TTarDirRec;
  lPos,lLen,lnumFilesTotal,lnumFilesCompleted,lPct: longint;
  lStr,lOutDir,lLocalDir,lFileName,lNewDir,lTarName : String;
begin
            lOutDir := extractfiledir(lTarFile);
            //next Count files for progress bar....
            lnumFilesTotal := 0;
            TA := TTarArchive.Create (lTarFile);
            TRY
               TA.Reset;
               TA.SetFilePos (0);
               TA.FindNext (DirRec);
               repeat
                     inc(lnumFilesTotal);
               until not TA.FindNext (DirRec);
            FINALLY
                   TA.Free;
            END;
            //finished counting files
            //next: extract files...
            lnumFilesCompleted := 0;
            //FProgress := 0;
            TA := TTarArchive.Create (lTarFile);
            TRY
               TA.Reset;
               TA.SetFilePos (0);
               TA.FindNext (DirRec);
            repeat
              inc(lNumFilesCompleted);
              {lPct := round(lNumFilesCOmpleted/lNumfilesTotal*100);
              if lPct > FProgress then begin //only update progress bar 100 times: do not waste time updating screen
                 FProgress := lPct;
                 DoOnProgress;
              end;}
              if DirRec.Name <> '' then begin
               //Screen.Cursor := crHourGlass;
               TRY
                  //filename change '/' to '\'
                  lTarName := '';
                  lLen := length(DirRec.name);
                  for lPos := 1 to lLen do begin
                     if (DirRec.Name[lPos]='/') or (DirRec.Name[lPos]='\') then
                        lTarName := lTarName + pathdelim//'\'
                     else if (DirRec.Name[lPos]=':') then

                     else
                         lTarName := lTarName + DirRec.Name[lPos];
                  end;
                  lFilename := lOutDir+pathdelim+lTarName;
                  lLocalDir := extractfiledir(lFileName);
                  if (DirectoryExists(lLocalDir)) then begin
                    if lOverwrite{(lProceed = mrYes) or (lProceed = mrYesToAll)} then begin
                     if (length(lFilename)>2) and (lFilename[length(lFilename)] = pathdelim)   then begin
                        lLen := length(lFilename)-1;
                        lStr := lFilename;
                        lFilename := '';
                        for lPos := 1 to lLen do
                            lFilename := lFilename+lStr[lPos];
                        if not DirectoryExists(lFilename) then begin
                           mkdir (lFilename);
                        end;
                     end else
                         TA.ReadFile (lFileName);
                    end; //proceed
                  end else begin
                     lLen := length(lTarName);
                     lPos := 1;
                     if (lLen >= 1) and (lTarName[1] = pathdelim) then inc(lPos);
                     lNewDir := lOutDir+pathdelim;
                     while lPos <= lLen do begin
                          if (lTarName[lPos] = pathdelim) then begin
                             //showmessage('creating directory:'+lNewDir);
                             if not DirectoryExists(lNewDir) then
                                mkdir(lNewDir);
                             lNewDir := lNewDir + pathdelim;
                          end else
                           lNewDir := lNewDir + lTarName[lPos];
                          inc(lPos);
                     end;
                     if (lFileName[length(lFileName)] <> pathdelim) and  (DirectoryExists(lLocalDir)) and (not Fileexists(lFileName)) then begin
                        TA.ReadFile (lFileName)
                     end;
                  end;
               FINALLY
                      //Screen.Cursor := crDefault;
               END;
              end;
            until not TA.FindNext (DirRec);
            FINALLY
                   TA.Free;
            END;
end;

function UnGZipFile(inname, outname:string):string;
var
ArchStream : TGZFileStream;
FileStream : TFileStream;
bytescopied: integer;
buf: array[0..65535] of byte;
chunksize : integer;
begin
chunksize := SizeOf(buf);
ArchStream := TGZFileStream.Create(inname,gzOpenRead);
FileStream := TFileStream.Create(outname,fmOpenWrite or fmCreate );
repeat
    bytescopied := ArchStream.read(buf,chunksize);
    FileStream.Write(buf,bytescopied) ;
 until bytescopied < chunksize;
   FileStream.Free;
   ArchStream.Free;
end;

function DeTGZ (lFilename: string): string;
var
 lPath,lName,lExt,lOutPath,lTempDir,lTarName,lDicomName: string;
begin
     result := '';
     if (not fileexists(lFilename)) or (not isTGZ(lFilename)) then
        exit;
     lPath := ExtractFilePath(lFilename);
     lExt := ExtractFileExt(lFilename);
     lName := ChangeFileExt (ExtractFileName (lFilename), '');
     lOutPath := lPath+lName;
     if DirectoryExists(lOutPath) then begin
     	if IsConsole then
           writeln( 'Unable to extract TGZ file - folder exists '+lOutpath)
        else
      		ShowMessage('Unable to extract TGZ file - folder exists '+lOutpath);
         exit;
     end;
     MkDir(lOutPath);
     lTempDir := lOutPath+pathdelim+'temp';
     MkDir(lTempDir);
     lTarName := lTempDir+Pathdelim+lName+'.tar';
     UnGZipFile( lFilename,lTarName);
     Extract (lTarName,true);
     deletefile(lTarName);
     result := lOutPath;
end;

end.
