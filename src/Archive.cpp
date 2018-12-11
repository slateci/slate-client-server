#include <cerrno>
#include <climits>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <sstream>

#include <sys/types.h>
#include <sys/stat.h> //chmod

#include <zlib.h>

void gzipDecompress(std::istream& src, std::ostream& dest){
	//https://tools.ietf.org/html/rfc1952 section 2.2
	unsigned char id[2];
	src.read((char*)id,2);
	if(src.eof() || src.fail() || id[0]!=0x1F || id[1]!=0x8B)
		throw std::runtime_error("Invalid gzip header");
	unsigned char cm;
	src.get((char&)cm);
	if(src.eof() || src.fail() || cm!=0x08)
		throw std::runtime_error("Unsupported gzip compression method");
	unsigned char flg;
	src.get((char&)flg);
	if(src.eof() || src.fail())
		throw std::runtime_error("Invalid gzip header");
	//particular flag values used below
	unsigned char mtime[4];
	src.read((char*)mtime,4);
	if(src.eof() || src.fail())
		throw std::runtime_error("Invalid gzip header");
	//we don't care what mtime is
	unsigned char xfl;
	src.get((char&)xfl);
	if(src.eof() || src.fail())
		throw std::runtime_error("Invalid gzip header");
	//we don't care what xfl is
	unsigned char os;
	src.get((char&)os);
	if(src.eof() || src.fail())
		throw std::runtime_error("Invalid gzip header");
	//we don't care what os is
	
	const static unsigned char ftextMask=0x1;
	const static unsigned char fhcrcMask=0x2;
	const static unsigned char fextraMask=0x4;
	const static unsigned char fnameMask=0x8;
	const static unsigned char fcommentMask=0x10;
	
	if(flg&fextraMask){ //if extra data is included, skip over it
		unsigned char xlenRaw[2];
		uint16_t xlen;
		src.read((char*)xlenRaw,2);
		if(src.eof() || src.fail())
			throw std::runtime_error("Invalid gzip header");
		xlen=((uint16_t)xlenRaw[0]) | ((uint16_t)xlenRaw[1]<<8);
		src.seekg(src.tellg()+(std::streamoff)xlen);
	}
	
	if(flg&fnameMask){ //if the original filename is included, skip over it
		char dummy;
		do{
			src.get(dummy);
		}while(dummy);
		if(src.eof() || src.fail())
			throw std::runtime_error("Invalid gzip header");
	}
	
	if(flg&fcommentMask){ //if comment is included, skip over it
		char dummy;
		do{
			src.get(dummy);
		}while(dummy);
		if(src.eof() || src.fail())
			throw std::runtime_error("Invalid gzip header");
	}
	
	if(flg&fhcrcMask){ //if a header checksum is included, skip over it
		unsigned char crc16Raw[2];
		src.read((char*)crc16Raw,2);
		if(src.eof() || src.fail())
			throw std::runtime_error("Invalid gzip header");
		//could check checksum here; not implemented
	}
	
	const std::streamsize readBlockSize=1024;
	//We expect the data to decompress, so we allocate the
	//output buffer to be larger than the input buffer.
	//According to Mark Adler (http://www.zlib.net/zlib_tech.html
	//retrieved August 29, 2014) zlib has a theoretical maximum
	//compression factor of 1032.
	const std::streamsize decompSize=1032*readBlockSize;
	std::unique_ptr<char[]> readBuffer(new char[readBlockSize]);
	std::unique_ptr<char[]> decompBuffer(new char[decompSize]);
	
	z_stream zs;
	zs.next_in = Z_NULL;
	zs.avail_in = 0;
	zs.next_out = (unsigned char*)decompBuffer.get();
	zs.avail_out = decompSize;
	zs.zalloc = Z_NULL; 
	zs.zfree = Z_NULL; 
	zs.opaque = Z_NULL;
	
	bool success=false;
	const int default_window_bits = 15;
	//invert window bits to indicate lack of header!
	int result=inflateInit2(&zs, -default_window_bits);
	if(result!=Z_OK)
		throw std::runtime_error("Failed to initialize zlib decompression");
	struct inflateCleanup{ //ensure that inflateEnd is always called on zs
		z_stream* s;
		inflateCleanup(z_stream* s):s(s){}
		~inflateCleanup(){ inflateEnd(s); }
	} c(&zs);
	while(!success || !src.eof()){
		if(!zs.avail_in){
			zs.next_in = (unsigned char*)readBuffer.get();
			//Collect more input
			//(For the first read we need a substantial amount of data,
			//because zlib wants to see the entire stream header at one time)
			while(zs.avail_in<readBlockSize && !src.eof()){
				src.read(readBuffer.get()+zs.avail_in,readBlockSize-zs.avail_in);
				zs.avail_in+=src.gcount();
			}
		}
		do{
			zs.next_out = (unsigned char*)decompBuffer.get();
			zs.avail_out = decompSize;
			result=inflate(&zs,Z_NO_FLUSH);
			if(result<Z_OK){
				std::ostringstream ss;
				ss << "Zlib decompression error: " << result;
				if(zs.msg!=Z_NULL)
					ss << " (" << zs.msg << ')';
				throw std::runtime_error(ss.str());
			}
			std::streamsize availData = decompSize-zs.avail_out;
			dest.write(decompBuffer.get(),availData);
		} while(zs.avail_out==0);
		if(result==Z_STREAM_END)
			success=true; //done!
	}
	if(!success)
		throw std::runtime_error("Unexpected end of compressed stream");
}

struct UStarHeader{
	bool blank;
	std::string name;
	uint64_t size;
	uint64_t mode;
	bool normalFile;
	
	UStarHeader():blank(true),normalFile(false){}
};

//read a string from a stream until the first NUL or space character
std::istream& getUStarString(std::istream& is, std::string& s){
	char c='\0';
	do{
		is.get(c);
		if(c=='\0' || c==' ')
			break;
		else
			s+=c;
	}while(!is.eof() && !is.fail());
	return is;
}

///parse a UStar tar header
std::istream& operator>>(std::istream& is, UStarHeader& header){
	std::streampos pos=is.tellg();
	getUStarString(is,header.name);
	if(header.name.size()>100)
		throw std::runtime_error("Invalid UStar header");
	
	pos+=100;
	is.seekg(pos);
	std::string tmpMode;
	getUStarString(is,tmpMode);
	if(is.eof() || is.fail() || tmpMode.size()>7)
		throw std::runtime_error("Invalid UStar header");
	header.mode=std::stoull(tmpMode,0,8); //convert from octal
	
	pos+=24;
	is.seekg(pos);
	std::string tmpSize;
	getUStarString(is,tmpSize);
	if(is.eof() || is.fail() || tmpSize.size()>11)
		throw std::runtime_error("Invalid UStar header");
	if(tmpSize.front()&(1u<<(CHAR_BIT-1)))
		throw std::runtime_error("Base-256 file size parsing not implmented");
	else
		header.size=std::stoull(tmpSize,0,8); //convert from octal
	
	pos+=32;
	is.seekg(pos);
	char type;
	is.read(&type,1);
	if(is.eof() || is.fail())
		throw std::runtime_error("Invalid UStar header");
	if(type!='0' && type!='\0')
		header.normalFile=false;
	else
		header.normalFile=true;
	
	pos+=101;
	is.seekg(pos);
	std::string ustar;
	getUStarString(is,ustar);
	if(is.eof() || is.fail())
		throw std::runtime_error("Invalid UStar header");
	if(ustar.empty())
		header.blank=true;
	else if(ustar=="ustar")
		header.blank=false;
	else
		throw std::runtime_error("Invalid UStar header");
	
	pos+=189;
	is.seekg(pos);
	std::string prefix;
	getUStarString(is,prefix);
	if(is.eof() || is.fail() || prefix.size()>155)
		throw std::runtime_error("Invalid UStar header");
	
	pos+=66;
	is.seekg(pos);
	if(is.eof() || is.fail())
		throw std::runtime_error("Invalid UStar header");
	
	return is;
}

bool extractFromUStar(std::stringstream& data, const std::string filename, const std::string destPath){
	//search the archive for the target file
	UStarHeader header;
	while(!data.eof()){
		data >> header;
		if(header.blank)
			return false;
		if(header.name==filename)
			break;
		std::streamoff roundedSize=header.size+(header.size%512 ? 512-header.size%512 : 0);
		data.seekg(data.tellg()+roundedSize);
	}
	if(!header.normalFile)
		throw std::runtime_error("This minimal tar extractor can only extract normal files");
	//extract the file
	std::ofstream outfile(destPath);
	if(!outfile)
		throw std::runtime_error("Unable to open "+destPath+" for writing");
	//find out where file data begins
	std::size_t idx=data.tellg();
	outfile.write(data.str().data()+idx,header.size);
	if(outfile.fail())
		throw std::runtime_error("Unable to write file data to "+destPath);
	int res=chmod(destPath.c_str(),header.mode);
	if(res!=0){
		res=errno;
		throw std::runtime_error("Failed to set "+destPath+" mode (error "+std::to_string(res)+")");
	}
	return true;
}