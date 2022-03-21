// The MIT License (MIT)
// 
// Copyright (c) 2020 Trevor Bakker 
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>

#define MAX_NUM_ARGUMENTS 6

#define WHITESPACE " \t\n"      // We want to split our command line up into tokens
                                // so we need to define what delimits our tokens.
                                // In this case  white space
                                // will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255    // The maximum command-line size

//Global BPB attributes:
int16_t BPB_BytesPerSec;
int8_t BPB_SecPerClus;
int16_t BPB_RsvdSecCnt;
int8_t BPB_NumFATS;
int32_t BPB_FATSz32;

//File pointer for the FAT32.img
FILE * fp;

struct __attribute__ ((__packed__)) DirectoryEntry
{
  char DIR_Name[11];
  uint8_t DIR_Attr;
  uint8_t Unused[8];
  uint16_t DIR_FirstClusterHigh;
  uint8_t Unused2[4];
  uint16_t DIR_FirstClusterLow;
  uint32_t DIR_FileSize;
};

//Global Directory struct for current directory(root/subdirectory)
struct DirectoryEntry dir[16];

//Compares the given name with the name in the directory struct
//CASE INSENSITIVE
//Returns: (-1) -> NOT FOUND , Index -> FOUND
int compare( char input[11])
{
  char expanded_name[12];
  memset( expanded_name, ' ', 12 );

  char *token = strtok( input, "." );

  strncpy( expanded_name, token, strlen( token ) );

  token = strtok( NULL, "." );

  if( token )
  {
    strncpy( (char*)(expanded_name+8), token, strlen(token ) );
  }

  expanded_name[11] = '\0';

  int i;
  for( i = 0; i < 11; i++ )
  {
    expanded_name[i] = toupper( expanded_name[i] );
  }
  for( i = 0; i < 16; i++)
  {
    if( strncmp( expanded_name, dir[i].DIR_Name, 11 ) == 0 )
    {
      return i;
    }
  }
  return -1;
}

/*
  Name: NextLB
  Purpose: Given a logical block address, look up into the first FAT and 
  return the logical block address of the block in the file. 
  If there is no further blocks then return -1
*/
int16_t NextLB( uint32_t sector )
{
  uint32_t FATAddress = ( BPB_BytesPerSec * BPB_RsvdSecCnt ) + (sector * 4);
  int16_t val;
  fseek( fp, FATAddress, SEEK_SET );
  fread( &val, 2, 1, fp );
  return val;
}

/*
  Name: LBAToOffset
  Parameters: The current sector number that points to a block of data
  Returns : The value of the address for that block of data
  Description : Finds the starting address of a block of data given the sector number
  corresponding to that data block
*/
int LBAToOffset(int32_t sector)
{
  return (( sector - 2 ) * BPB_BytesPerSec) + (BPB_BytesPerSec * BPB_RsvdSecCnt) + (BPB_NumFATS * BPB_FATSz32 * BPB_BytesPerSec);
}

int main()
{
  //Tracks the State->(OPEN/CLOSE) of the file
  bool file_opened = false;
  
  char * cmd_str = (char*) malloc( MAX_COMMAND_SIZE );

  while( 1 )
  {
    // Print out the mfs prompt
    printf ("mfs> ");

    // Read the command from the commandline.  The
    // maximum command that will be read is MAX_COMMAND_SIZE
    // This while command will wait here until the user
    // inputs something since fgets returns NULL when there
    // is no input
    while( !fgets (cmd_str, MAX_COMMAND_SIZE, stdin) );

    /* Parse input */
    char *token[MAX_NUM_ARGUMENTS];

    int   token_count = 0;                                 
                                                           
    // Pointer to point to the token
    // parsed by strsep
    char *arg_ptr;                                         
                                                           
    char *working_str  = strdup( cmd_str );                

    // we are going to move the working_str pointer so
    // keep track of its original value so we can deallocate
    // the correct amount at the end
    char *working_root = working_str;

    // Tokenize the input stringswith whitespace used as the delimiter
    while ( ( (arg_ptr = strsep(&working_str, WHITESPACE ) ) != NULL) && 
              (token_count<MAX_NUM_ARGUMENTS))
    {
      token[token_count] = strndup( arg_ptr, MAX_COMMAND_SIZE );

      if( strlen( token[token_count] ) == 0 )
      {
        token[token_count] = NULL;
      }
        token_count++;
    }

    //Compare token[0] with the required commands
    //MATCHED: Performs specific actions
    //UNMATCHED: Prints Error Message
    if( strcmp(token[0], "open") == 0 )
    {
      if( token[1] == NULL )
      {
        printf("No file name entered.\n");
      }  
      else if(token_count > 3)
      {
        printf("Error: Too many parameters.\n");
      } 
      else 
      {
        if( file_opened == false )
        { 
          //Opens the entered file
          fp = fopen(token[1], "r");

          if( fp == NULL )
          {
            printf("Error: File system image not found.\n");  
          } 
          else
          {
            //FILE FOUND AND OPENED
            file_opened = true;
            
            //Populates the (GLobal) bpb attributes 
            fseek( fp, 11, SEEK_SET );
            fread( &BPB_BytesPerSec, 2, 1, fp ); 
                         
            fseek( fp, 13, SEEK_SET );
            fread( &BPB_SecPerClus, 1, 1, fp);
             
            fseek( fp, 14, SEEK_SET );
            fread( &BPB_RsvdSecCnt, 2, 1, fp);

            fseek( fp, 16, SEEK_SET );
            fread( &BPB_NumFATS, 1, 1, fp);

            fseek( fp, 36, SEEK_SET );
            fread( &BPB_FATSz32, 4, 1, fp);
            
            //Populates the directory struct of the root directory
            fseek( fp, (BPB_NumFATS * BPB_FATSz32 * BPB_BytesPerSec) + (BPB_RsvdSecCnt * BPB_BytesPerSec), SEEK_SET);
            fread( dir, 16, sizeof( struct DirectoryEntry ), fp );
          }
        }
        else
        {
          printf("Error: File system already open.\n");
        }
      }
    }
    else if( strcmp(token[0], "close") == 0 )
    {
      if(token_count > 2)
      {
        printf("Error: Too many parameters.\n");
      }
      else
      {
        //Closes file IF: file opened OR file_opened = true
        //Error Message IF: file NOT opened OR file_opened = false
        if( file_opened == true )
        {
          //File Closed
          fclose(fp);
          //Updates the boolean
          file_opened = false; 
        }
        else
        {
          printf("Error: File system not open.\n");
        }
      }
    }
    else if((strcmp(token[0],"exit") == 0) || (strcmp(token[0],"quit") == 0))
    {
      if(token_count > 2)
      {
        printf("Error: Too many parameters.\n");
      }
      else
      {
        //Exits the program
        exit(0);
      }
    }
    else if( file_opened == false )
    {
      //Checks if a file is opened
      //Disables all the commands except: open, close, and exit
      printf("Error: File system image must be opened first.\n");
    }
    else if( strcmp(token[0], "bpb" ) == 0 )
    {
      if(token_count > 2)
      {
        printf("Error: Too many parameters.\n");
      }
      else
      {
        //Prints bpb attributes
        printf("BPB_BytesPerSec : %d\n", BPB_BytesPerSec);
        printf("BPB_BytesPerSec : %x\n\n", BPB_BytesPerSec);
        printf("BPB_SecPerClus : %d\n", BPB_SecPerClus);
        printf("BPB_SecPerClus : %x\n\n", BPB_SecPerClus);
        printf("BPB_RsvdSecCnt : %d\n", BPB_RsvdSecCnt);
        printf("BPB_RsvdSecCnt : %x\n\n", BPB_RsvdSecCnt);
        printf("BPB_NumFATS : %d\n", BPB_NumFATS);
        printf("BPB_NumFATS : %x\n\n", BPB_NumFATS);
        printf("BPB_FATSz32 : %d\n", BPB_FATSz32);
        printf("BPB_FATSz32 : %x\n\n", BPB_FATSz32);
      }
    }
    else if( strcmp(token[0], "ls" ) == 0 )
    {
      if(token_count > 2)
      {
        printf("Error: Too many parameters.\n");
      }
      else
      {
        int i = 0;
        //Loops over all the directory struct
        for(i = 0; i < 16; i++)
        {
          //ONLY prints the files specified for the assignment
          if( dir[i].DIR_Name[0] != '\x00' && dir[i].DIR_Name[0] != '\xe5' && (dir[i].DIR_Attr  == '\x01' || dir[i].DIR_Attr == '\x10' || dir[i].DIR_Attr == '\x20') )
          {
            printf("%.11s\n", dir[i].DIR_Name );
          }
        }
      }
    }    
    else if( strcmp(token[0], "cd") == 0 )
    {
      if( token[1] == NULL )
      {
        printf("Error: Directory Name Not Entered.\n");
      } 
      else if(token_count > 3)
      {
        printf("Error: Too many parameters.\n");
      }
      else
      {
        int x = 0;
        //Uppercase the entered file name for comparision
        for( x = 0; x < strlen(token[1]); x++ )
        {
          token[1][x] = toupper(token[1][x]);
        }

        int i = 0;
      
        bool file_matched = false;
        int address = 0;
        int temp_low_cluster = 0;
        int temp_attr = 0;
        //Loops over the elements of current directory struct
        //UNTIL a name is matched
        while(i < 16 && file_matched == false)
        {
          //Removes the junk values and NULL terminates the name
          char temp[9];
          memcpy ( temp, dir[i].DIR_Name, 8 ); 
          temp[8] = '\0';
          int j = 7;        

          while(j > 0)
          {
            if(temp[j] == ' ')
            {
              temp[j] = '\0';
            }
            else
            {
              break;
            }
            j--;
          } 
          //Copies the null terminated 'temp' name to 'UPPER_temp'
          //Uppercase the characters of 'UPPER_temp'
          char UPPER_temp[9];
          strcpy(UPPER_temp, temp);
          for( x = 0; x < strlen(UPPER_temp); x++ )
          {
            UPPER_temp[x] = toupper( UPPER_temp[x] );
          }
          //IF filename MATCHES then update the temp_lower_cluster_number and temp_attr
          //SET the address, using LBAToOffset, to the data cluster of the matched directory
          if( strcmp(token[1], UPPER_temp) == 0 )
          {
            file_matched = true;
            temp_low_cluster = dir[i].DIR_FirstClusterLow;
            temp_attr = dir[i].DIR_Attr;          

            address = LBAToOffset( dir[i].DIR_FirstClusterLow );
          } 
          i++;
        }
        //Checks if the matched name is a directory
        //IF the name -> '..' hold the adress of the root directory i.e. lower_cluster = 0,
        //Set the address for root directory
        if( file_matched == true && temp_attr == '\x10')
        { 
          if ( temp_low_cluster == 0 )
          {
            address = (BPB_NumFATS * BPB_FATSz32 * BPB_BytesPerSec) + (BPB_RsvdSecCnt * BPB_BytesPerSec);
          }
          fseek( fp, address, SEEK_SET);
          fread( dir, 16, sizeof( struct DirectoryEntry ), fp );
        }
        else if( file_matched == false )
        {
          printf("Error: No such directory found.\n");
        }
        else
        {
          printf("Error: Not a directory.\n");
        }
      }
    }
    else if( strcmp(token[0], "stat") == 0 )   
    {
      if( token[1] == NULL )
      {
        printf("Error: No file/directory name entered.\n");
      }
      else if(token_count > 3)
      {
        printf("Error: Too many parameters.\n");
      }
      else
      {
        int i = 0;
        //Pass the entered name to the function 'compare'
        //ret_val = -1 ; if NOT FOUND
        //ret_val = index; if FOUND
        int ret_val = compare(token[1]);
        if(  ret_val != -1 )
        {
          //Print the stat if MATCHED
          printf("File attribute: %d\n", dir[ret_val].DIR_Attr);
          printf("Size: %d\n", dir[ret_val].DIR_FileSize);
          printf("Starting cluster number: %d\n", dir[ret_val].DIR_FirstClusterLow);
        }
        else
        {
          printf("Error: No such file found.\n");
        }
      }
    }
    else if( strcmp(token[0], "read") == 0 )
    {
      if(token[1] == NULL || token[2] == NULL || token[3] == NULL)
      {
        printf("Error: Not enough parameters.\n");
      }
      else if(token_count > 5)
      {
        printf("Error: Too many parameters.\n");
      }
      else
      {
        //Pass the entered name to the function 'compare'
        //ret_val = -1 ; if NOT FOUND
        //ret_val = index; if FOUND
        int ret_val = compare(token[1]);

        //If FOUND; read the entered file
        if(ret_val != -1)
        { 
          //Lower cluster number
          int sector = dir[ret_val].DIR_FirstClusterLow;

          //Cluster Number from where the read starts
          int start_clusters = atoi(token[2]) / BPB_BytesPerSec;

          //Position in the cluster from where the read starts
          int offset = atoi(token[2]) % BPB_BytesPerSec;

          //Total Number of cluster to be read
          int num_clusters = ((atoi(token[3]) + offset)/ BPB_BytesPerSec) + 1;

          //Total Number of bytes to be read
          int num_bytes = atoi(token[3]);

          bool out_of_range = false;
          int i = 0;

          //Loops to reach the cluster from where the read starts
          for( i = 0; i < start_clusters; i++ )
          {
            sector = NextLB(sector);
            //Breaks if entered 'position' is out-of-range
            if(sector == -1)
            {
              //Breaks if entered 'position' is out-of-range
              out_of_range = true;
              break;
            }
          }  
          //IF case: Reached the starting cluster
          if(out_of_range == false)
          {
            char buffer;
            //Loops for num_cluster, total number of cluster to be read, times
            for( i = 0; i < num_clusters; i++)
            {
              //Move the file pointer to the designated offset of the cluster
              fseek( fp, LBAToOffset(sector) + offset, SEEK_SET );
              int j = 0;
              for( j = offset; j < BPB_BytesPerSec; j++ )
              {
                //Decrease the total number of byte to be read 
                num_bytes--;
                //Store 1 byte in the buffer and print it out
                fread( &buffer, 1, 1, fp);
                printf("%x  ", buffer);
                //IF case: Break if the required number of bytes is read
                if(num_bytes == 0)
                {
                  break;
                }
              }
              //IF case: Break if the required number of bytes is read
              if(num_bytes == 0)
              {
                break;
              }

              //Reset the position in the cluster from where the read starts.
              offset = 0;
              
              //Move to the next cluster
              sector = NextLB(sector);
              //IF Case: Break if next cluster is not found
              if(sector == -1)
              {
                break;
              }
              
            }
            printf("\n");
          }
          else
          {
            printf("Error: Out of blocks.\n");
          }
          
        }
        else
        {
          printf("Error: No such file.\n");
        }

      }
    }   
    else if( strcmp(token[0], "get") == 0 )
    {
      if(token[1] == NULL)
      {
        printf("Error: Not enough parameters.\n");
      }
      else if(token_count > 4)
      {
        printf("Error: Too many parameters.\n");
      }
      else 
      {
        char temp_name[12];
        strcpy(temp_name, token[1]);
        //Search for the entered file 
        //ret_val = -1 (NOT FOUND), ret_val = index (FOUND)
        int ret_val = compare(token[1]);

        if(ret_val != -1)
        {
          unsigned char buffer[BPB_BytesPerSec];
          FILE * newFile;
          if(token[2] == NULL) //get command without renaming the file
          {  
            //Create a new file with the same name
            newFile = fopen(temp_name, "w");
          }
          else // get command with renaming the file
          {
            //Create a new file with the given name 
            newFile = fopen(token[2], "w");
          }

          //Starting Lower cluster Number of the file
          int sector = dir[ret_val].DIR_FirstClusterLow;

          //Total File size OR Total File size to be read
          int FileSize = dir[ret_val].DIR_FileSize;
          
          //Loops until the last cluster is reached
          while(FileSize >= BPB_BytesPerSec)
          {            
            //Move the file pointer to the required address
            fseek( fp, LBAToOffset(sector), SEEK_SET );
            //Read all the bytes from the current cluster into the buffer
            //AND Write the buffer into the NEWFILE
            fread( buffer, 1, BPB_BytesPerSec, fp);
            fwrite( buffer, 1, BPB_BytesPerSec, newFile);   
            //Decrease the total file size to be read
            FileSize = FileSize - BPB_BytesPerSec;
            //Move to the next cluster
            sector = NextLB(sector);
          }
          //Handles Last cluster
          if(FileSize)
          {
            //Move the file pointer to theaddress of last(only) cluster
            fseek( fp, LBAToOffset(sector), SEEK_SET );
            //Reads the file and stores the remaining bytes into the buffer
            fread( buffer, 1, FileSize, fp);
            //Write the buffer into the NEWFILE
            fwrite( buffer, 1, FileSize, newFile);
          }
          //File Closed
          fclose(newFile);
        }
        else
        {
          printf("Error: No such File\n");
        }
      }  
    }
    else
    {
      //Command NOT Matched 
      printf("Error: Invalid command.\n");
    }  

    free( working_root );

  }
  return 0;
}
