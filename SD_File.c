/*----------------------------------------------------------------------------
 *      RL-ARM - FlashFS
 *----------------------------------------------------------------------------
 *      Name:    SD_FILE.C
 *      Purpose: File manipulation example program
 *----------------------------------------------------------------------------
 *      This code is part of the RealView Run-Time Library.
 *      Copyright (c) 2004-2011 KEIL - An ARM Company. All rights reserved.
 *---------------------------------------------------------------------------*/

#include <RTL.h>                      /* RTL kernel functions & defines      */
#include <stdio.h>                    /* standard I/O .h-file                */
#include <ctype.h>                    /* character functions                 */
#include <string.h>                   /* string and memory functions         */
#include "File_Config.h"
#include "SD_File.h"
#include "LCD.h"
#include <LPC23xx.H>
#define MEM_LEN 1024

//Defining port numbers
#define PLAY 0x2000
#define STOP 0x1000
#define BACK 0x0800
#define FORW 0x0400



__irq void T0_IRQHandler(void);
__irq void ADC_IRQHandler(void);
__irq void EINT3_IRQHandler  (void);



//Audio File being read
struct audioData {
  long long int totSize;
  U64 readSize;
  long int numChannels;
  long long int sampleRate;
  long int sampleSize;
  long long int Subchunk1Size;
  long int PCM;
  long long int curPos;
  FILE * f;
  char md;
  char bufrs[MEM_LEN];
  int pos;
  int buf;
  int swi;
  int vol;
  int ct;
  
  int stat;
}
curAudio;




/* Command Functions */
static void cmd_capture(char * par);
static void cmd_type(char * par);
static void cmd_rename(char * par);
static void cmd_copy(char * par);
static void cmd_delete(char * par);
static void cmd_dir(char * par);
static void cmd_format(char * par);
static void cmd_help(char * par);
static void cmd_fill(char * par);
static void cmd_play(char * par);

/* Local constants */
static
const char intro[] =
  "\n\n\n\n\n\n\n\n"
"+-----------------------------------------------------------------------+\n"
"|                SD/MMC Card File Manipulation example                  |\n";
static
const char help[] =
  "+ command ------------------+ function ---------------------------------+\n"
"| CAP \"fname\" [/A]          | captures serial data to a file            |\n"
"|                           |  [/A option appends data to a file]       |\n"
"| FILL \"fname\" [nnnn]       | create a file filled with text            |\n"
"|                           |  [nnnn - number of lines, default=1000]   |\n"
"| TYPE \"fname\"              | displays the content of a text file       |\n"
"| REN \"fname1\" \"fname2\"     | renames a file 'fname1' to 'fname2'       |\n"
"| COPY \"fin\" [\"fin2\"] \"fout\"| copies a file 'fin' to 'fout' file        |\n"
"|                           |  ['fin2' option merges 'fin' and 'fin2']  |\n"
"| DEL \"fname\"               | deletes a file                            |\n"
"| DIR \"[mask]\"              | displays a list of files in the directory |\n"
"| FORMAT [label [/FAT32]]   | formats Flash Memory Card                 |\n"
"|                           | [/FAT32 option selects FAT32 file system] |\n"
"| PLAY                      | Display and play song                     |\n"
"| HELP  or  ?               | displays this help                        |\n"
"+---------------------------+-------------------------------------------+\n";

static
const SCMD cmd[] = {
  "CAP",
  cmd_capture,
  "TYPE",
  cmd_type,
  "REN",
  cmd_rename,
  "COPY",
  cmd_copy,
  "DEL",
  cmd_delete,
  "DIR",
  cmd_dir,
  "FORMAT",
  cmd_format,
  "HELP",
  cmd_help,
  "FILL",
  cmd_fill,
  "?",
  cmd_help,
  "PLAY",
  cmd_play
};

#define CMD_COUNT(sizeof(cmd) / sizeof(cmd[0]))

/* Local variables */
static char in_line[160];

/* Local Function Prototypes */
static void dot_format(U64 val, char * sp);
static char * get_entry(char * cp, char ** pNext);
static void init_card(void);


void clearAudData(){
    
    fclose(curAudio.f);
    curAudio.totSize = 0;
    curAudio.curPos = 0;
    curAudio.md = 0;
    curAudio.readSize = 0;
    curAudio.numChannels = 0;
    curAudio.sampleRate = 0;
    curAudio.sampleSize = 0;
    curAudio.Subchunk1Size = 0;
    curAudio.PCM = 0;
   
    curAudio.pos = 0;
    curAudio.buf = 0;
    curAudio.swi = 0;
    curAudio.vol = 0;
    curAudio.ct = 0;

    //curAudio.stat = 0;
    
    VICIntEnClr = (1 << 4);
    T0TCR = 0;
}

/*----------------------------------------------------------------------------
 *        Process input string for long or short name entry
 *---------------------------------------------------------------------------*/
static char * get_entry(char * cp, char ** pNext) {
  char * sp, lfn = 0, sep_ch = ' ';

  if (cp == NULL) {
    /* skip NULL pointers          */
    * pNext = cp;
    return (cp);
  }

  for (;* cp == ' ' || * cp == '\"'; cp++) {
    /* skip blanks and starting  " */
    if ( * cp == '\"') {
      sep_ch = '\"';
      lfn = 1;
    }
    * cp = 0;
  }

  for (sp = cp;* sp != CR && * sp != LF; sp++) {
    if (lfn && * sp == '\"') break;
    if (!lfn && * sp == ' ') break;
  }

  for (;* sp == sep_ch || * sp == CR || * sp == LF; sp++) {
    * sp = 0;
    if (lfn && * sp == sep_ch) {
      sp++;
      break;
    }
  }

  * pNext = ( * sp) ? sp : NULL; /* next entry                  */
  return (cp);
}

/*----------------------------------------------------------------------------
 *        Print size in dotted fomat
 *---------------------------------------------------------------------------*/
static void dot_format(U64 val, char * sp) {

  if (val >= (U64) 1e9) {
    sp += sprintf(sp, "%d.", (U32)(val / (U64) 1e9));
    val %= (U64) 1e9;
    sp += sprintf(sp, "%03d.", (U32)(val / (U64) 1e6));
    val %= (U64) 1e6;
    sprintf(sp, "%03d.%03d", (U32)(val / 1000), (U32)(val % 1000));
    return;
  }
  if (val >= (U64) 1e6) {
    sp += sprintf(sp, "%d.", (U32)(val / (U64) 1e6));
    val %= (U64) 1e6;
    sprintf(sp, "%03d.%03d", (U32)(val / 1000), (U32)(val % 1000));
    return;
  }
  if (val >= 1000) {
    sprintf(sp, "%d.%03d", (U32)(val / 1000), (U32)(val % 1000));
    return;
  }
  sprintf(sp, "%d", (U32)(val));
}

/*----------------------------------------------------------------------------
 *        Capture serial data to file
 *---------------------------------------------------------------------------*/
static void cmd_capture(char * par) {
  char * fname, * next;
  BOOL append, retv;
  FILE * f;

  fname = get_entry(par, & next);
  if (fname == NULL) {
    printf("\nFilename missing.\n");
    return;
  }
  append = __FALSE;
  if (next) {
    par = get_entry(next, & next);
    if ((strcmp(par, "/A") == 0) || (strcmp(par, "/a") == 0)) {
      append = __TRUE;
    } else {
      printf("\nCommand error.\n");
      return;
    }
  }
  printf((append) ? "\nAppend data to file %s" :
    "\nCapture data to file %s", fname);
  printf("\nPress ESC to stop.\n");
  f = fopen(fname, append ? "a" : "w"); /* open a file for writing           */
  if (f == NULL) {
    printf("\nCan not open file!\n"); /* error when trying to open file    */
    return;
  }
  do {
    retv = getline(in_line, sizeof(in_line));
    fputs(in_line, f);
  } while (retv == __TRUE);
  fclose(f); /* close the output file               */
  printf("\nFile closed.\n");
}

/*----------------------------------------------------------------------------
 *        Create a file and fill it with some text
 *---------------------------------------------------------------------------*/
static void cmd_fill(char * par) {
  char * fname, * next;
  FILE * f;
  int i, cnt = 1000;

  fname = get_entry(par, & next);
  if (fname == NULL) {
    printf("\nFilename missing.\n");
    return;
  }
  if (next) {
    par = get_entry(next, & next);
    if (sscanf(par, "%d", & cnt) == 0) {
      printf("\nCommand error.\n");
      return;
    }
  }

  f = fopen(fname, "w"); /* open a file for writing           */
  if (f == NULL) {
    printf("\nCan not open file!\n"); /* error when trying to open file    */
    return;
  }
  for (i = 0; i < cnt; i++) {
    fprintf(f, "This is line # %d in file %s\n", i, fname);
  }
  fclose(f); /* close the output file               */
  printf("\nFile closed.\n");
}

/*----------------------------------------------------------------------------
 *        Read file and dump it to serial window
 *---------------------------------------------------------------------------*/
static void cmd_type(char * par) {
  char * fname, * next;
  FILE * f;
  int ch;

  fname = get_entry(par, & next);
  if (fname == NULL) {
    printf("\nFilename missing.\n");
    return;
  }
  printf("\nRead data from file %s\n", fname);
  f = fopen(fname, "r"); /* open the file for reading           */
  if (f == NULL) {
    printf("\nFile not found!\n");
    return;
  }

  while ((ch = fgetc(f)) != EOF) {
    /* read the characters from the file   */
    putchar(ch); /* and write them on the screen        */
  }
  fclose(f); /* close the input file when done      */
  printf("\nFile closed.\n");
}

/*----------------------------------------------------------------------------
 *        Rename a File
 *---------------------------------------------------------------------------*/
static void cmd_rename(char * par) {
  char * fname, * fnew, * next, dir;

  fname = get_entry(par, & next);
  if (fname == NULL) {
    printf("\nFilename missing.\n");
    return;
  }
  fnew = get_entry(next, & next);
  if (fnew == NULL) {
    printf("\nNew Filename missing.\n");
    return;
  }
  if (strcmp(fname, fnew) == 0) {
    printf("\nNew name is the same.\n");
    return;
  }

  dir = 0;
  if ( * (fname + strlen(fname) - 1) == '\\') {
    dir = 1;
  }

  if (frename(fname, fnew) == 0) {
    if (dir) {
      printf("\nDirectory %s renamed to %s\n", fname, fnew);
    } else {
      printf("\nFile %s renamed to %s\n", fname, fnew);
    }
  } else {
    if (dir) {
      printf("\nDirectory rename error.\n");
    } else {
      printf("\nFile rename error.\n");
    }
  }
}

/*----------------------------------------------------------------------------
 *        Copy a File
 *---------------------------------------------------------------------------*/
static void cmd_copy(char * par) {
  char * fname, * fnew, * fmer, * next;
  FILE * fin, * fout;
  U32 cnt, total;
  char buf[512];
  BOOL merge;

  fname = get_entry(par, & next);
  if (fname == NULL) {
    printf("\nFilename missing.\n");
    return;
  }
  fmer = get_entry(next, & next);
  if (fmer == NULL) {
    printf("\nNew Filename missing.\n");
    return;
  }
  fnew = get_entry(next, & next);
  if (fnew != NULL) {
    merge = __TRUE;
  } else {
    merge = __FALSE;
    fnew = fmer;
  }
  if ((strcmp(fname, fnew) == 0) ||
    (merge && strcmp(fmer, fnew) == 0)) {
    printf("\nNew name is the same.\n");
    return;
  }

  fin = fopen(fname, "r"); /* open the file for reading           */
  if (fin == NULL) {
    printf("\nFile %s not found!\n", fname);
    return;
  }

  if (merge == __FALSE) {
    printf("\nCopy file %s to %s\n", fname, fnew);
  } else {
    printf("\nCopy file %s, %s to %s\n", fname, fmer, fnew);
  }
  fout = fopen(fnew, "w"); /* open the file for writing           */
  if (fout == NULL) {
    printf("\nFailed to open %s for writing!\n", fnew);
    fclose(fin);
    return;
  }

  total = 0;
  while ((cnt = fread( & buf, 1, 512, fin)) != 0) {
    fwrite( & buf, 1, cnt, fout);
    total += cnt;
  }
  fclose(fin); /* close input file when done          */

  if (merge == __TRUE) {
    fin = fopen(fmer, "r"); /* open the file for reading           */
    if (fin == NULL) {
      printf("\nFile %s not found!\n", fmer);
    } else {
      while ((cnt = fread( & buf, 1, 512, fin)) != 0) {
        fwrite( & buf, 1, cnt, fout);
        total += cnt;
      }
      fclose(fin);
    }
  }
  fclose(fout);
  dot_format(total, & buf[0]);
  printf("\n%s bytes copied.\n", & buf[0]);
}

/*----------------------------------------------------------------------------
 *        Delete a File
 *---------------------------------------------------------------------------*/
static void cmd_delete(char * par) {
  char * fname, * next, dir;

  fname = get_entry(par, & next);
  if (fname == NULL) {
    printf("\nFilename missing.\n");
    return;
  }

  dir = 0;
  if ( * (fname + strlen(fname) - 1) == '\\') {
    dir = 1;
  }

  if (fdelete(fname) == 0) {
    if (dir) {
      printf("\nDirectory %s deleted.\n", fname);
    } else {
      printf("\nFile %s deleted.\n", fname);
    }
  } else {
    if (dir) {
      printf("\nDirectory %s not found or not empty.\n", fname);
    } else {
      printf("\nFile %s not found.\n", fname);
    }
  }
}

/*----------------------------------------------------------------------------
 *        Print a Flash Memory Card Directory
 *---------------------------------------------------------------------------*/
static void cmd_dir(char * par) {
  U64 fsize;
  U32 files, dirs, i;
  char temp[32], * mask, * next, ch;
  FINFO info;

  mask = get_entry(par, & next);
  if (mask == NULL) {
    mask = "*.*";
  }

  printf("\nFile System Directory...");
  files = 0;
  dirs = 0;
  fsize = 0;
  info.fileID = 0;
  while (ffind(mask, & info) == 0) {
    if (info.attrib & ATTR_DIRECTORY) {
      i = 0;
      while (strlen((const char * ) info.name + i) > 41) {
        ch = info.name[i + 41];
        info.name[i + 41] = 0;
        printf("\n%-41s", & info.name[i]);
        info.name[i + 41] = ch;
        i += 41;
      }
      printf("\n%-41s    <DIR>       ", & info.name[i]);
      printf("  %02d.%02d.%04d  %02d:%02d",
        info.time.day, info.time.mon, info.time.year,
        info.time.hr, info.time.min);
      dirs++;
    } else {
      dot_format(info.size, & temp[0]);
      i = 0;
      while (strlen((const char * ) info.name + i) > 41) {
        ch = info.name[i + 41];
        info.name[i + 41] = 0;
        printf("\n%-41s", & info.name[i]);
        info.name[i + 41] = ch;
        i += 41;
      }
      printf("\n%-41s %14s ", & info.name[i], temp);
      printf("  %02d.%02d.%04d  %02d:%02d",
        info.time.day, info.time.mon, info.time.year,
        info.time.hr, info.time.min);
      fsize += info.size;
      files++;
    }
  }
  
  if (info.fileID == 0) {
    printf("\nNo files...");
  } else {
    dot_format(fsize, & temp[0]);
    printf("\n              %9d File(s)    %21s bytes", files, temp);
  }
  dot_format(ffree(""), & temp[0]);
  if (dirs) {
    printf("\n              %9d Dir(s)     %21s bytes free.\n", dirs, temp);
  } else {
    printf("\n%56s bytes free.\n", temp);
  }
}

/*----------------------------------------------------------------------------
 *        Format a Flash Memory Card
 *---------------------------------------------------------------------------*/
static void cmd_format(char * par) {
  char * label, * next, * opt;
  char arg[20];
  U32 retv;

  label = get_entry(par, & next);
  if (label == NULL) {
    label = "KEIL";
  }
  strcpy(arg, label);
  opt = get_entry(next, & next);
  if (opt != NULL) {
    if ((strcmp(opt, "/FAT32") == 0) || (strcmp(opt, "/fat32") == 0)) {
      strcat(arg, "/FAT32");
    }
  }
  printf("\nFormat Flash Memory Card? [Y/N]\n");
  retv = getkey();
  if (retv == 'y' || retv == 'Y') {
    /* Format the Card with Label "KEIL". "*/
    if (fformat(arg) == 0) {
      printf("Memory Card Formatted.\n");
      printf("Card Label is %s\n", label);
    } else {
      printf("Formatting failed.\n");
    }
  }
}

/*----------------------------------------------------------------------------
 *        Display Command Syntax help
 *---------------------------------------------------------------------------*/
static void cmd_help(char * par) {
  printf(help);
}

static void cmd_play(char * par) {

  char * fname, * next;
  int wi = 0;
  int ch;
  long long int i = 0;
  int stat = 1;
  unsigned long long int temp = 0;
  char head[] = "RIFF";
  const char head2[] = "WAVE";
  const char head3[] = "fmt ";
  const char head4[] = "data";

  printf("Playing file");
  fname = get_entry(par, & next);
  if (fname == NULL) {
    printf("\nFilename missing.\n");
    return;
  }
  printf("\nRead data from file %s\n", fname);

  curAudio.vol = 2;

  curAudio.f = fopen(fname, "r"); /* open the file for reading           */
  if (curAudio.f == NULL) {
    printf("\nFile not found!\n");
    return;
  }

  while (i < 4 && stat == 1) {

    ch = fgetc(curAudio.f);
    if (ch != head[i])
      stat = 0;
    i++;
  }

  i = 0;

  curAudio.readSize = 0;
  while (i < 4 && stat == 1) {
    temp = fgetc(curAudio.f);
    temp = temp << (i * 8);
    curAudio.totSize += temp;
    i++;
  }
  temp = 0;

  printf("\n%lli\n", curAudio.totSize);
  i = 0;

  while (i < 4 && stat == 1) {

    ch = fgetc(curAudio.f);
    if (ch != head2[i])
      stat = 0;
    putchar(ch);
    i++;
  }
  i = 0;
  while (i < 4 && stat == 1) {

    ch = fgetc(curAudio.f);
    if (ch != head3[i])
      stat = 0;
    putchar(ch);
    i++;
  }

  if (stat == 0) {
    printf("\nNot Wave File\n");
    fclose(curAudio.f);
    return;
  }
  i = 0;
  curAudio.Subchunk1Size = 0;
  while (i < 4) {
    temp = fgetc(curAudio.f);
    curAudio.Subchunk1Size += (temp << (8 * i));
    i++;
  }

  printf("\nChunkSize = %lli\n", curAudio.Subchunk1Size);
  if (curAudio.Subchunk1Size != 16)
    printf("/nThis is not PCM, not sure how to proceed\n");

  i = 0;
  curAudio.PCM = 0;
  while (i < 2) {
    temp = fgetc(curAudio.f);
    curAudio.PCM += (temp << (8 * i));
    i++;
  }

  printf("\nPCM = %li\n", curAudio.PCM);
  if (curAudio.PCM != 1)
    printf("/nThis is not PCM = 1, not sure how to proceed, some compression\n");

  i = 0;
  curAudio.numChannels = 0;
  while (i < 2) {
    temp = fgetc(curAudio.f);
    curAudio.numChannels += (temp << (8 * i));
    i++;
  }

  printf("\nNumber of Channels = %li\n", curAudio.numChannels);
  if (curAudio.numChannels == 1) {
    curAudio.md = 0;
  } else if (curAudio.numChannels == 2) {
    printf("This is Dual channel, weird output will occur\n");
    curAudio.md = 1;
  }

  i = 0;
  curAudio.sampleRate = 0;
  while (i < 4) {
    temp = fgetc(curAudio.f);
    curAudio.sampleRate += (temp << (8 * i));
    i++;
  }
  printf("\nSample Rate = %lli\n", curAudio.sampleRate);

  i = 0;
  while (i < 6) {
    ch = fgetc(curAudio.f);
    putchar(ch);
    i++;
  }

  i = 0;
  curAudio.sampleSize = 0;
  while (i < 2) {
    temp = fgetc(curAudio.f);
    curAudio.sampleSize += (temp << (8 * i));
    i++;
  }
  printf("\nSample Size = %li\n", curAudio.sampleSize);
  if (curAudio.sampleSize == 16)
    curAudio.md |= 2;
  else if (curAudio.sampleSize != 8) {
    printf("breaking, unknown sample size");
    return;
  }

  i = 0;
  while (i < 4 && stat == 1) {

    ch = fgetc(curAudio.f);
    if (ch != head4[i]) {
      i = 0;
      continue;
    }
    putchar(ch);
    i++;
  }

  if (stat == 0) {
    printf("\nSomething wrong happend\n");
    fclose(curAudio.f);
    return;
  }

  curAudio.readSize = 0;
  i = 0;
  head[0] = 0;
  head[1] = 0;
  head[2] = 0;
  head[3] = 0;

  while (i < 4) {
    head[i] = fgetc(curAudio.f);
    printf("%i\n", head[i]);
    i++;
  }

  curAudio.readSize = (U64)(head[0]) + ((U64)(head[1]) << 8) + ((U64)(head[2]) << 16) + ((U64)(head[3]) << 24);
  printf("\nTo Read %lli Bytes now\n", curAudio.readSize);

  //WE HAVE TO SET DAC FOR PUTTING OUT ALARMS
  PINSEL1 |= 0x200000;

  curAudio.curPos = 0;

  /* Enable and setup timer interrupt, start timer                            */
  T0MR0 = (12000000 / curAudio.sampleRate) - 1; /* 1msec = 12000-1 at 12.0 MHz , made it x1000 for seconds*/
  //T0MR0 = 1499;
  T0TCR = 1; /* Timer0 Enable               */

  VICIntEnable = (1 << 4); /* Enable Timer0 Interrupt     */

  curAudio.pos = 0;
  //curAudio.buf = 1;
  curAudio.swi = 1;
  temp = curAudio.readSize;
  stat = 0;
  
  
  curAudio.stat = 1;
  
  
  while (temp && (curAudio.stat&2) == 0) {
    if (curAudio.swi == 1) {
      VICIntEnClr = (1 << 4);
      i = fread(curAudio.bufrs, 1, MEM_LEN, curAudio.f);
      temp -= i;
      curAudio.swi = 0;
      curAudio.pos = 0;
      AD0CR |= 0x01000000; /* Start A/D Conversion               */
      VICIntEnable = (1 << 4);

    }
    
    if (curAudio.curPos >= curAudio.readSize) {
      break;
    }

  }

  printf("%lli   %lli\n", curAudio.curPos, curAudio.readSize);
  
  clearAudData();
  
  printf("\nFile closed.\n");

}
/*----------------------------------------------------------------------------
 *        Initialize a Flash Memory Card
 *---------------------------------------------------------------------------*/
static void init_card(void) {
  U32 retv;

  while ((retv = finit(NULL)) != 0) {
    /* Wait until the Card is ready*/
    if (retv == 1) {
      printf("\nSD/MMC Init Failed");
      printf("\nInsert Memory card and press key...\n");
      getkey();
    } else {
      printf("\nSD/MMC Card is Unformatted");
      strcpy( & in_line[0], "KEIL\r\n");
      cmd_format( & in_line[0]);
    }
  }
}

/*----------------------------------------------------------------------------
 *        Main: 
 *---------------------------------------------------------------------------*/
int main(void) {
  char * sp, * cp, * next;
  U32 i;

  init_comm(); /* init communication interface*/


    lcd_init();
    lcd_clear();

    set_cursor (0, 0);
    lcd_print("Song Play");
    
    
  printf(intro); /* display example info        */
  printf(help);

  init_card();

  T0MCR = 3; /* Interrupt and Reset on MR0  */
  VICVectAddr4 = (unsigned long) T0_IRQHandler; /* Set Interrupt Vector        */
  VICVectCntl4 = 15; /* use it for Timer0 Interrupt */

  /*for volume reading-----------------------------------------------------------------------------------------------*/
  PCONP |= (1 << 12); /* Enable power to AD block    */
  PINSEL1 |= 0x4000; /* AD0.0 pin function select   */
  AD0INTEN = (1 << 0); /* CH0 enable interrupt        */
  AD0CR = 0x00200301; /* Power up, PCLK/4, sel AD0.0 */
  VICVectAddr18 = (unsigned long) ADC_IRQHandler; /* Set Interrupt Vector       */
  VICVectCntl18 = 14; /* use it for ADC Interrupt    */
  VICIntEnable = (1 << 18);



    IO2_INT_EN_F = PLAY | STOP | FORW | BACK ;//To enable interrupt on play, stop, next, back buttons, USING PORT2
    VICVectAddr17  = (unsigned long)EINT3_IRQHandler;/* Set Interrupt Vector        */
    VICVectCntl17  = 13;                          /* use it for EINT3 Interrupt */
    //EXTMODE = 0x09; // EINT0 and 3 are edge sensitive
    //EXTPOLAR = 0x09;// Rising edge
    EXTINT = 0x0F;
    VICIntEnable  = (1  << 17);                   /* Enable EINT3 Interrupt     */
    
    
    
    
  while (1) {
    // printf("\nCmd> "); /* display prompt              */
    // fflush(stdout);
    // /* get command line input      */
    // if (getline(in_line, sizeof(in_line)) == __FALSE) {
      // continue;
    // }

    // sp = get_entry( & in_line[0], & next);
    // if ( * sp == 0) {
      // continue;
    // }
    // for (cp = sp;* cp && * cp != ' '; cp++) {
      // * cp = toupper( * cp); /* command to upper-case       */
    // }
    // for (i = 0; i < CMD_COUNT; i++) {
      // if (strcmp(sp, (const char * ) & cmd[i].val)) {
        // continue;
      // }
      // init_card(); /* check if card is removed    */
      // cmd[i].func(next); /* execute command function    */
      // break;
    // }
    // if (i == CMD_COUNT) {
      // printf("\nCommand error\n");
    // }
    
    printf("Showing Directory");
    cmd_dir("");
    cmd_play("A.WAV");
  }
}

/*----------------------------------------------------------------------------
 * end of file
 *---------------------------------------------------------------------------*/

__irq void T0_IRQHandler(void) {

  unsigned int temp = 0;

  switch (curAudio.md) {
      case 0:
        /* Mono, 8bit */
        temp = curAudio.bufrs[curAudio.pos] << 8;
        curAudio.pos += 1;
        curAudio.curPos++;
        break;
      case 1:
        /* Stereo, 8bit */
        temp = (curAudio.bufrs[curAudio.pos] + curAudio.bufrs[curAudio.pos + 1]) << 7;
        curAudio.pos += 2;
        curAudio.curPos += 2;
        break;
      case 2:
        /* Mono, 16bit */
        temp = ((curAudio.bufrs[curAudio.pos + 1] << 8) + (curAudio.bufrs[curAudio.pos])) ^ 0x8000;
        curAudio.pos += 2;
        curAudio.curPos += 2;
        break;
      default:
        /* Stereo, 16bit */
        temp = ((curAudio.bufrs[curAudio.pos + 1] << 8) + (curAudio.bufrs[curAudio.pos])) ^ 0x8000;
        temp += ((curAudio.bufrs[curAudio.pos + 3] << 8) + (curAudio.bufrs[curAudio.pos + 2])) ^ 0x8000;
        temp >>= 1;
        curAudio.pos += 4;
        curAudio.curPos += 4;
  }
  if (curAudio.pos == MEM_LEN)
    curAudio.swi = 1;
  temp >>= (7 - curAudio.vol);
  DACR = temp;
  temp = 0;

  T0IR = T0IR; /* Clear interrupt flag               */
  VICVectAddr = 0; /* Acknowledge Interrupt              */
}

__irq void ADC_IRQHandler(void) {

  curAudio.vol = (AD0DR0 >> 6) & 0x3FF; /* Read Conversion Result             */
  curAudio.vol >>= 7;
  VICVectAddr = 0; /* Acknowledge Interrupt              */
}

__irq void EINT3_IRQHandler  (void){

    int i =0;
    long long int portRe = 0;

    //read IO2IntStatF and see what you want to do 
    //use IO2IntClr to clear that interrupt 

    while(i < 100000){
        i++;
    }

    portRe = (IO2_INT_STAT_F & ~FIO2PIN);


    if(portRe == 0){
        IO2_INT_CLR = 0xFFFFF;   
        EXTINT = 0x08;  //Clear EINT3 int
        VICVectAddr = 0;                      /* Acknowledge Interrupt              */
        return;
    }

    lcd_clear();


    if(portRe & PLAY){
        set_cursor (0, 0);
        //resume/play/start playing    
        if ((curAudio.stat&1)==0){	
            lcd_print("PLAY");
            curAudio.stat |= 01;
            VICIntEnable = (1 << 4);
        }else if ((curAudio.stat&1)==1){
            lcd_print("PAUSE");
            curAudio.stat &= 0xFE;
            VICIntEnClr = (1 << 4);
        }
    }
    else if(portRe & STOP){
        //stop
        set_cursor (0, 0);
        lcd_print("STOP");
        curAudio.stat |= 2;
        //IO2_INT_CLR = STOP;       
    }
    else if(portRe & FORW){
        //Next song please
        set_cursor (0, 0);
        lcd_print("FORW");
        curAudio.stat |= 4+2;
        //IO2_INT_CLR = FORW;       
    }
    else if(portRe & BACK){
        //previous song please
        set_cursor (0, 0);
        lcd_print("BACK");
        curAudio.stat |= (8+2);
        //IO2_INT_CLR = BACK;       
    }else{
        set_cursor (0, 0);
        lcd_print("Error!");			
    }

    IO2_INT_CLR = 0xFFFFF;   
    EXTINT = 0x08;  //Clear EINT3 int
    VICVectAddr = 0;                      /* Acknowledge Interrupt              */
}