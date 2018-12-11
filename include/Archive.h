#ifndef SLATE_ARCHIVE_H
#define SLATE_ARCHIVE_H

#include <istream>
#include <ostream>
#include <string>

///decompress gzipped data from one stream to another
void gzipDecompress(std::istream& src, std::ostream& dest);

///extract one file from a UStar tarball and write it to disk
///\param data the archive from which the file should be extracted
void extractFromUStar(std::stringstream& data, const std::string filename, 
                      const std::string destPath);

#endif //SLATE_ARCHIVE_H