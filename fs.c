// ============================================================================
// fs.c - user FileSytem API
// ============================================================================

#include "bfs.h"
#include "fs.h"

// ============================================================================
// Close the file currently open on file descriptor 'fd'.
// ============================================================================
i32 fsClose(i32 fd) { 
  i32 inum = bfsFdToInum(fd);
  bfsDerefOFT(inum);
  return 0; 
}



// ============================================================================
// Create the file called 'fname'.  Overwrite, if it already exsists.
// On success, return its file descriptor.  On failure, EFNF
// ============================================================================
i32 fsCreate(str fname) {
  i32 inum = bfsCreateFile(fname);
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Format the BFS disk by initializing the SuperBlock, Inodes, Directory and 
// Freelist.  On succes, return 0.  On failure, abort
// ============================================================================
i32 fsFormat() {
  FILE* fp = fopen(BFSDISK, "w+b");
  if (fp == NULL) FATAL(EDISKCREATE);

  i32 ret = bfsInitSuper(fp);               // initialize Super block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitInodes(fp);                  // initialize Inodes block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitDir(fp);                     // initialize Dir block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitFreeList();                  // initialize Freelist
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitOFT();                  	   // initialize OFT
  if (ret != 0) { fclose(fp); FATAL(ret); }

  fclose(fp);
  return 0;
}


// ============================================================================
// Mount the BFS disk.  It must already exist
// ============================================================================
i32 fsMount() {
  FILE* fp = fopen(BFSDISK, "rb");
  if (fp == NULL) FATAL(ENODISK);           // BFSDISK not found
  fclose(fp);
  return 0;
}



// ============================================================================
// Open the existing file called 'fname'.  On success, return its file 
// descriptor.  On failure, return EFNF
// ============================================================================
i32 fsOpen(str fname) {
  i32 inum = bfsLookupFile(fname);        // lookup 'fname' in Directory
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Read 'numb' bytes of data from the cursor in the file currently fsOpen'd on
// File Descriptor 'fd' into 'buf'.  On success, return actual number of bytes
// read (may be less than 'numb' if we hit EOF).  On failure, abort
// ============================================================================
i32 fsRead(i32 fd, i32 numb, void* buf) {
    // Validate file descriptor and get inode number
    i32 inum = bfsFdToInum(fd);
    if (inum < 0) FATAL(EBADINUM); // Invalid inode number

    // Validate Open File Table entry
    i32 ofte = bfsFindOFTE(inum);
    if (ofte < 0) FATAL(EOFTFULL); // Open File Table full or invalid OFTE entry

    // Get cursor position and file size
    i32 cursor = g_oft[ofte].curs;
    i32 fileSize = bfsGetSize(inum);
    printf("fsRead: fileSize = %d, cursor = %d\n", fileSize, cursor);

    if (cursor >= fileSize) return 0; // EOF reached

    // Calculate the number of bytes to read
    i32 bytesToRead = (cursor + numb > fileSize) ? fileSize - cursor : numb;
    printf("fsRead: bytesToRead = %d\n", bytesToRead);

    // Read data block-by-block
    i32 bytesRead = bfsRead(inum, cursor / BYTESPERBLOCK, buf);
    if (bytesRead < 0) FATAL(EBADREAD); // Error reading from BFS disk
    printf("fsRead: bytesRead = %d\n", bytesRead);

    // Update cursor position and return bytes read
    bfsSetCursor(inum, cursor + bytesRead);
    return bytesRead;
}


// ============================================================================
// Move the cursor for the file currently open on File Descriptor 'fd' to the
// byte-offset 'offset'.  'whence' can be any of:
//
//  SEEK_SET : set cursor to 'offset'
//  SEEK_CUR : add 'offset' to the current cursor
//  SEEK_END : add 'offset' to the size of the file
//
// On success, return 0.  On failure, abort
// ============================================================================
i32 fsSeek(i32 fd, i32 offset, i32 whence) {

  if (offset < 0) FATAL(EBADCURS);
 
  i32 inum = bfsFdToInum(fd);
  i32 ofte = bfsFindOFTE(inum);
  
  switch(whence) {
    case SEEK_SET:
      g_oft[ofte].curs = offset;
      break;
    case SEEK_CUR:
      g_oft[ofte].curs += offset;
      break;
    case SEEK_END: {
        i32 end = fsSize(fd);
        g_oft[ofte].curs = end + offset;
        break;
      }
    default:
        FATAL(EBADWHENCE);
  }
  return 0;
}



// ============================================================================
// Return the cursor position for the file open on File Descriptor 'fd'
// ============================================================================
i32 fsTell(i32 fd) {
  return bfsTell(fd);
}



// ============================================================================
// Retrieve the current file size in bytes.  This depends on the highest offset
// written to the file, or the highest offset set with the fsSeek function.  On
// success, return the file size.  On failure, abort
// ============================================================================
i32 fsSize(i32 fd) {
  i32 inum = bfsFdToInum(fd);
  return bfsGetSize(inum);
}



// ============================================================================
// Write 'numb' bytes of data from 'buf' into the file currently fsOpen'd on
// filedescriptor 'fd'.  The write starts at the current file offset for the
// destination file.  On success, return 0.  On failure, abort
// ============================================================================
i32 fsWrite(i32 fd, i32 numb, void* buf) {
    // Validate file descriptor and get inode number
    i32 inum = bfsFdToInum(fd);
    if (inum < 0) FATAL(EBADINUM); // Invalid inode number

    // Validate Open File Table entry
    i32 ofte = bfsFindOFTE(inum);
    if (ofte < 0) FATAL(EOFTFULL); // Open File Table full or invalid OFTE entry

    // Get the current cursor position
    i32 cursor = bfsTell(fd);

    // Write data block-by-block
    i32 bytesWritten = 0;
    while (bytesWritten < numb) {
        i32 fbn = cursor / BYTESPERBLOCK;          // File block number
        i32 offset = cursor % BYTESPERBLOCK;       // Offset within the block
        i32 toWrite = BYTESPERBLOCK - offset;      // Bytes to write in the current block

        if (toWrite > (numb - bytesWritten)) {
            toWrite = numb - bytesWritten;         // Adjust for remaining bytes
        }

        // Allocate a block if necessary
        i32 dbn = bfsFbnToDbn(inum, fbn);
        if (dbn < 0) {
            dbn = bfsAllocBlock(inum, fbn);
            if (dbn < 0) FATAL(EDISKFULL); // No free disk space
        }

        // Buffer for the block
        char block[BYTESPERBLOCK];
        memset(block, 0, BYTESPERBLOCK);          // Zero out the block
        bioRead(dbn, block);                      // Read the existing block

        // Copy data into the block
        memcpy(block + offset, buf + bytesWritten, toWrite);

        // Write the block back to the disk
        bioWrite(dbn, block);

        // Update counters and cursor
        bytesWritten += toWrite;
        cursor += toWrite;
    }

    // Update cursor and file size
    bfsSetCursor(inum, cursor);
    if (cursor > bfsGetSize(inum)) bfsSetSize(inum, cursor);

    return bytesWritten;
}


