#include <Archive.h>

#include <cerrno>
#include <climits>
#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <queue>
#include <stdexcept>
#include <sstream>

#include <unistd.h>
#include <sys/types.h>
#include <sys/dir.h>
#include <sys/stat.h> //chmod

#include <zlib.h>

#include <FileSystem.h>

std::string decodeBase64(const std::string& coded){
	//table used by boost::archive::iterators::detail::to_6_bit
	static const signed char lookupTable[] = {
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
		52,53,54,55,56,57,58,59,60,61,-1,-1,-1, 0,-1,-1,
		-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
		15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
		-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
		41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
	};
	std:size_t outLen=(coded.size()*3)/4;
	std::string decoded(outLen,'\0');
	char* outData=&decoded.front();
	unsigned char curBits=0;
	for(const unsigned char next : coded){
		if(next>=128 || lookupTable[next]==-1)
			throw std::runtime_error("Illegal base64 character: '"+std::string(1,next)+"'");
		unsigned char newBits=lookupTable[next];
		unsigned char putBits=0;
		//shove as many bits into the current byte as will fit
		{
			putBits=std::min(CHAR_BIT-curBits,6);
			unsigned int mask=(((1u<<putBits)-1)<<(6-putBits));
			*outData|=((newBits&mask)>>(6-putBits))<<(CHAR_BIT-curBits-putBits);
			curBits+=putBits;
		}
		if(curBits==CHAR_BIT){ //if the byte is full, advance to the next
			curBits=0;
			outData++;
			//if more bits need to be written put them in now
			if(putBits<6){
				putBits=6-putBits;
				unsigned int mask=(1u<<putBits)-1;
				*outData|=(newBits&mask)<<(CHAR_BIT-putBits);
				curBits=putBits;
			}
		}
	}
	return decoded;
}

std::string encodeBase64(const std::string& raw){
	const char lookupTable[65]=
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789"
		"+/";
	std:size_t outLen=std::ceil((raw.size()*4)/3.);
	std::string encoded(outLen,'\0');
	unsigned char availBits=CHAR_BIT;
	size_t inputIdx=0;
	for(size_t i=0; i<outLen; i++){
		unsigned char getBits=0;
		unsigned char lutIdx=0;
		//grab as many bits as possible from the current byte
		{
			getBits=std::min((unsigned char)6,availBits);
			unsigned int mask=((1u<<getBits)-1)<<(availBits-getBits);
			lutIdx|=((raw[inputIdx]&mask)>>(availBits-getBits))<<(6-getBits);
			availBits-=getBits;
		}
		if(availBits==0){ //if that byte is exhausted, advance to the next
			inputIdx++;
			availBits=CHAR_BIT;
			//if more bits are needed get them now
			if(getBits<6){
				getBits=6-getBits;
				unsigned int mask=((1u<<getBits)-1)<<(availBits-getBits);
				lutIdx|=((raw[inputIdx]&mask)>>(availBits-getBits));
				availBits-=getBits;
			}
		}
		encoded[i]=lookupTable[lutIdx];
	}
	return encoded;
}

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

void gzipCompress(std::istream& src, std::ostream& dest){
	//write a header as described by https://tools.ietf.org/html/rfc1952 section 2.2
	dest.put(0x1F); //ID1
	dest.put(0x8B); //ID2
	dest.put(0x08); //CM (compression method)
	dest.put(0x00); //FLG (flags; none set)
	unsigned char mtime[4]={0};
	dest.write((const char*)mtime,sizeof(mtime));
	dest.put(2); //XFL - max compression
	dest.put((char)255); //OS - Unknown (don't care)
	
	//set up CRC according to https://tools.ietf.org/html/rfc1952#section-8.1.1.6.2
	/* Table of CRCs of all 8-bit messages. */
	uint32_t crc_table[256];
	{
		int n, k;
        for(n = 0; n < 256; n++){
			uint32_t c = (uint32_t) n;
			for(k = 0; k < 8; k++){
				if(c & 1)
					c = 0xedb88320L ^ (c >> 1);
				else
					c = c >> 1;
			}
			crc_table[n] = c;
        }
	}
	//initialize CRC to zero
	uint32_t crc=0;
	//function to update the running CRC
	auto updateCRC=[&](unsigned char* buf, int len){
		uint32_t c = crc ^ 0xffffffffL;
		int n;
		for (n = 0; n < len; n++)
			c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
        crc = c ^ 0xffffffffL;
	};
	
	const std::size_t inBlockSize=128*1024;
	const std::size_t outBlockSize=129*1024; //slightly lager than input in case of pathology
	std::unique_ptr<unsigned char[]> readBuffer(new unsigned char[inBlockSize]);
	std::unique_ptr<unsigned char[]> compBuffer(new unsigned char[outBlockSize]);
	
	uint32_t totalSize=0;
	z_stream zs;
	zs.zalloc = Z_NULL;
	zs.zfree = Z_NULL;
	zs.opaque = Z_NULL;
	int result=deflateInit2(&zs, 
							Z_BEST_COMPRESSION,
							Z_DEFLATED, //required
							-15, //window bits, negative to suppress header
							8, //memory level
							Z_DEFAULT_STRATEGY);
	if(result!=Z_OK)
		throw std::runtime_error("zlib initilization failed");
	
	int flush;
	do{
		src.read((char*)readBuffer.get(),inBlockSize);
		if(src.fail() && !src.eof())
			throw std::runtime_error("Input stream failure");
		zs.avail_in=src.gcount();
		updateCRC(readBuffer.get(),zs.avail_in);
		totalSize+=zs.avail_in;
		flush = src.eof() ? Z_FINISH : Z_NO_FLUSH;
		zs.next_in = readBuffer.get();
		do{
			zs.avail_out = outBlockSize;
			zs.next_out = compBuffer.get();
			result = deflate(&zs,flush);
			if(result==Z_STREAM_ERROR)
				throw std::runtime_error("zlib compression failed");
			std::size_t have=outBlockSize-zs.avail_out;
			dest.write((char*)compBuffer.get(),have);
			if(dest.fail())
				throw std::runtime_error("Output stream failure");
		}while(zs.avail_out == 0);
	}while(flush!=Z_FINISH);
	deflateEnd(&zs);
	//TODO: this will produce incorrect results on big-endian systems
	dest.write((const char*)&crc,sizeof(crc));
	dest.write((const char*)&totalSize,sizeof(totalSize));
}

struct header_posix_ustar {
	enum typeCode{
		RegularFile = 0,
		HardLink = 1,
		SymLink = 2,
		CharDevice = 3,
		BlockDevice = 4,
		Directory = 5,
		FIFONode = 6,
	};
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char mtime[12];
	char checksum[8];
	char typeflag[1];
	char linkname[100];
	char magic[6];
	char version[2];
	char uname[32];
	char gname[32];
	char devmajor[8];
	char devminor[8];
	char prefix[155];
	char pad[12];
	
	header_posix_ustar(){
		memset(this,0,sizeof(header_posix_ustar)); //fill with NULs
	}
	
	header_posix_ustar(const std::string& fileName, unsigned long long fileSize, typeCode fileType=RegularFile){
		if(fileSize>077777777777LL)
			throw std::length_error("Specified file size >= 8 GB");
		
		memset(this,0,sizeof(header_posix_ustar)); //fill with NULs
		if(fileName.size()>100){
			if(fileName.size()>256)
				throw std::runtime_error("Filename too long to be stored in POSIX UStar format");
			//try to find a point where the name can be split, given that we must:
			// 1. split on a '/'
			// 2. put no more than 100 bytes in the file name field
			// 3. put no more than 155 bytes in the prefix field
			// This looks roughly like this, except that we get one free byte 
			//for the '/' on which we split:
			// path:   aaaaaaaa/bbbbbbbb
			// prefix: |<- 155 ->|
			// name:         |<- 100 ->|
			std::size_t splitIdx;
			bool validSplit=false;
			for(splitIdx=fileName.size()-101; splitIdx<=156; splitIdx++){
				if(fileName[splitIdx]=='/'){
					validSplit=true;
					break;
				}
			}
			if(!validSplit)
				throw std::runtime_error("Unable to find a prefix/filename split which satisfies POSIX UStar constraints");
			std::copy_n(fileName.begin(),splitIdx,prefix);
			std::copy(fileName.begin()+splitIdx+1,fileName.end(),name);
		}
		else //fileName just fits in the default field
			std::copy(fileName.begin(),fileName.end(),name);
		//TODO: don't hard-code file mode
		sprintf(mode,"%07o",0644);
		sprintf(uid,"%07o",getuid());//leave uid filled with NULs
		sprintf(gid,"%07o",getgid());//leave gid filled with NULs
		sprintf(size,"%011o",(unsigned int)fileSize);
		sprintf(mtime,"%011o",(unsigned int)time(NULL));
		//typeflag[0]=directory?'5':'0';
		typeflag[0]='0'+fileType;
		//leave linkname filled with NULs
		sprintf(magic,"ustar  "); //overwrites version with " \0"
		//leave uname filled with NULs
		//leave gname filled with NULs
		//leave devmajor filled with NULs
		//leave devminor filled with NULs
		//leave prefix filled with NULs
		//leave pad filled with NULs
		
		setChecksum();
	}
	
	void setChecksum(){
		memset(checksum,' ',8);
		unsigned int sum=0;
		unsigned char* ptr=reinterpret_cast<unsigned char*>(this);
		for(unsigned int i=0; i<sizeof(header_posix_ustar); i++){
			sum+=ptr[i];
		}
		if(sum>0777777)
			throw std::runtime_error("Checksum overflow");
		sprintf(checksum,"%6o",sum);
		checksum[7]=' ';
	}
	
	bool checksumValid() const{
		//compute the current checksum
		unsigned int sum=0;
		const unsigned char* ptr=reinterpret_cast<const unsigned char*>(this);
		for(unsigned int i=0; i<sizeof(header_posix_ustar); i++){
			if(i>=offsetof(header_posix_ustar,checksum) && 
			  i<offsetof(header_posix_ustar,checksum)+sizeof(checksum))
				sum+=' ';
			else
				sum+=ptr[i];
		}
		//try to extract the stored checksum value
		unsigned int storedSum=0;
		{
			bool foundDigits=false;
			for(int i=0; i<8; i++){
				if(!foundDigits){
					if(std::isspace(checksum[i]))
						continue;
					if(checksum[i]>='0' && checksum[i]<='7'){
						storedSum=checksum[i]-'0';
						foundDigits=true;
					}
					else //invalid characters
						return false;
				}
				else{
					if(checksum[i]=='\0' || checksum[i]==' ')
						break;
					if(checksum[i]>='0' && checksum[i]<='7')
						storedSum=8*storedSum+checksum[i]-'0';
					else //invalid characters
						return false;
				}
			}
		}
		return sum==storedSum;
	}
	
	bool isEmpty() const{
		const char* t = (char*)this;
		return std::none_of(t,t+sizeof(header_posix_ustar),[](char c){ return !!c; });
	}
	
	std::string getName() const{
		std::string result;
		if(prefix[0]){
			//figure out how long prefix is, bearing in mind that it may not be 
			//NUL terminated
			unsigned int len=1;
			for(len=1; len<sizeof(prefix); len++){
				if(!prefix[len])
					break;
			}
			result=std::string(prefix,len);
			result+='/';
		}
		//repeat to figure out name's length
		unsigned int len=0;
		for(; len<sizeof(name); len++){
			if(!name[len])
				break;
		}
		if(len)
			result+=std::string(name,len);
		return result;
	}
};

static_assert(sizeof(header_posix_ustar)==512, "a UStar header must be 512 bytes");

///converts from tar type indicator flags to FileRecord::fileType values
TarReader::FileRecord::fileType typeForTarTypeFlag(char typeFlag);

TarReader::FileRecord::FileRecord():
type(REGULAR_FILE),data(""),mode(0644){}
TarReader::FileRecord::FileRecord(fileType t, int m):
type(t),data(""),mode(m){}
TarReader::FileRecord::FileRecord(fileType t, const std::string& d, int m):
type(t),data(d),mode(m){}
TarReader::FileRecord::FileRecord(fileType t, unsigned long long dataSize, std::istream& dataSrc, int m):
type(t),data(""),mode(m){
	data.reserve(dataSize);
	for(unsigned long i=0; i<dataSize; i++)
		data+=dataSrc.get();
}

const std::string& TarReader::FileRecord::getData() const{
	return(data);
}

unsigned long long TarReader::FileRecord::getFileSize() const{
	if(type == REGULAR_FILE)
		return(data.size());
	return(0);
}

TarReader::FileRecord::fileType typeForTarTypeFlag(char typeFlag){
	switch(typeFlag){
		case '0':
			return(TarReader::FileRecord::REGULAR_FILE);
		case '1':
			return(TarReader::FileRecord::HARD_LINK);
		case '2':
			return(TarReader::FileRecord::SYMBOLIC_LINK);
		case '3':
			return(TarReader::FileRecord::CHARACTER_DEVICE);
		case '4':
			return(TarReader::FileRecord::BLOCK_DEVICE);
		case '5':
			return(TarReader::FileRecord::DIRECTORY);
		case '6':
			return(TarReader::FileRecord::FIFO);
		case '7':
			return(TarReader::FileRecord::RESERVED);
		default:
			return(TarReader::FileRecord::REGULAR_FILE);
	}
}

TarReader::TarReader(std::istream& source):src(source),fileEnded(false){}

std::unique_ptr<std::istream> TarReader::streamForFile(const std::string& name){
	std::map< std::string,FileRecord >::const_iterator it = files.find(name);
	if(it==files.end()){
		if(src.eof())
			throw missing_file_exception("File '"+name+"' not found");
		readFiles(name);
		it = files.find(name);
		if(it==files.end())
			throw missing_file_exception("File '"+name+"' not found");
	}
	return(std::unique_ptr<std::istream>(new std::istringstream(it->second.getData())));
}

const std::string& TarReader::stringForFile(const std::string& name){
	std::map< std::string,FileRecord >::const_iterator it = files.find(name);
	if(it==files.end()){
		if(src.eof())
			throw missing_file_exception("File '"+name+"' not found");
		readFiles(name);
		it = files.find(name);
		if(it==files.end())
			throw missing_file_exception("File '"+name+"' not found");
	}
	return(it->second.getData());
}

TarReader::FileRecord::fileType TarReader::typeForFile(const std::string& name){
	std::map< std::string,FileRecord >::const_iterator it = files.find(name);
	if(it==files.end()){
		if(src.eof())
			throw missing_file_exception("File '"+name+"' not found");
		readFiles(name);
		it = files.find(name);
		if(it==files.end())
			throw missing_file_exception("File '"+name+"' not found");
	}
	return(it->second.getType());
}

int TarReader::modeForFile(const std::string& name){
	std::map< std::string,FileRecord >::const_iterator it = files.find(name);
	if(it==files.end()){
		if(src.eof())
			throw missing_file_exception("File '"+name+"' not found");
		readFiles(name);
		it = files.find(name);
		if(it==files.end())
			throw missing_file_exception("File '"+name+"' not found");
	}
	return(it->second.getMode());
}

void TarReader::dropFile(const std::string& name){
	files.erase(name);
	//DEBUG//std::cout << "\tDropped " << name << "; " << files.size() << " files still held in memory" << std::endl;
}

std::string TarReader::nextFile(){
	while(true){
		std::string name=readFiles("");
		if(name=="")
			break;
		std::map< std::string,FileRecord >::const_iterator it = files.find(name);
		if(it!=files.end())
			return(name);
	}
	return("");
}

std::string TarReader::nextFileOfType(FileRecord::fileType type){
	while(true){
		std::string name=readFiles("");
		if(name=="")
			break;
		std::map< std::string,FileRecord >::const_iterator it = files.find(name);
		if(it!=files.end() && it->second.getType()==type)
			return(name);
	}
	return("");
}

bool TarReader::eof() const{
	return(fileEnded);
}

std::string TarReader::readFiles(const std::string& target){
	header_posix_ustar h;
	long long size;
	unsigned short nEmpty=0;
	std::string name;
	while(true){
		name="";
		src.read((char*)&h,sizeof(h));
		
		if(src.eof() || src.fail()){
			fileEnded=true;
			break;
		}
		
		if(h.isEmpty()){
			nEmpty++;
			if(nEmpty==2){
				fileEnded=true;
				break;
			}
			else
				continue;
		}
		else
			nEmpty = 0;
			
		if(!h.checksumValid())
			throw std::runtime_error("Invalid UStar header checksum");
		
		auto properlyTerminated=[](const char* field, unsigned int maxLen){
			for(unsigned int i=0; i<maxLen; i++)
				if(field[i]==0 || field[i]==' ')
					return true;
			return false;
		};
		if(!properlyTerminated(h.size,12))
			throw std::runtime_error("Improperly terminated file size field in UStar header");
		sscanf(h.size,"%llo",&size);
		if(size>0x1FFFFFFFF)
			throw std::runtime_error("Overlarge file size in UStar header");
		if(!properlyTerminated(h.mode,8))
			throw std::runtime_error("Improperly terminated file size field in UStar header");
		int mode;
		sscanf(h.mode,"%o",&mode);
		
		FileRecord::fileType type = typeForTarTypeFlag(*h.typeflag);
		name=h.getName();
		
		if(type==FileRecord::REGULAR_FILE)
			files.insert(std::make_pair(name,FileRecord(type,size,src,mode)));
		else if(type == FileRecord::SYMBOLIC_LINK)
			files.insert(std::make_pair(name,FileRecord(type,h.linkname,mode)));
		else
			files.insert(std::make_pair(name,FileRecord(type,mode)));
		
		if(size%512)
			src.ignore(512-(size%512));
		if(name==target || target=="")
			break;
	}
	return(name);
}

void TarReader::extractToFileSystem(const std::string& prefix, bool dropAfterExtracting){
	//TODO: this won't play well with any previous calls to other extraction functions. 
	//We can't just dump out the contents of files because the order of directories
	//and their contents matters, and we don't store that. For now just error out. 
	if(!files.empty())
		throw std::runtime_error("extractToFileSystem does not correctly handle already extracted files");
	while(!eof()){
		std::string baseFileName=readFiles("");
		if(baseFileName.empty())
			break;
		std::string fileName=prefix+"/"+baseFileName;
		
		const FileRecord& file=files[baseFileName];
		//TODO: set permissions on extracted files
		switch(file.getType()){
			case FileRecord::REGULAR_FILE:
			{
				std::ofstream outfile(fileName);
				if(!outfile)
					throw std::runtime_error("Unable to open "+fileName+" for writing");
				std::ostreambuf_iterator<char> out_it (outfile);
				const std::string& fileData=file.getData();
				std::copy(fileData.begin(), fileData.end(), out_it);
				break;
			}
			case FileRecord::SYMBOLIC_LINK:
			{
				int err=symlink(file.getData().c_str(),fileName.c_str());
				if(err){
					err=errno;
					throw std::runtime_error("Unable to extract symlink: error "+std::to_string(err));
				}
				break;
			}
			case FileRecord::DIRECTORY:
			{
				mkdir_p(fileName,0755);
				break;
			}
			default:
				throw std::runtime_error("Extraction not implemented for file type "+std::to_string(file.getType()));
		}
		if(dropAfterExtracting)
			dropFile(baseFileName);
	}
}

void TarWriter::appendFile(const std::string& filepath, const std::string& data){
	if(ended)
		throw std::runtime_error("Cannot append to an ended tar stream");
	header_posix_ustar header(filepath,data.length());
	sink.write((const char*)&header,sizeof(header));
	sink.write((const char*)data.c_str(),data.length());
	unsigned int remainder=data.length()%512;
	if(remainder){
		unsigned int padding=512-remainder;
		for(unsigned int i=0; i<padding; i++)
			sink.put('\0');
	}
}

void TarWriter::appendDirectory(const std::string& path){
	if(ended)
		throw std::runtime_error("Cannot append to an ended tar stream");
	header_posix_ustar header(path,0,header_posix_ustar::Directory);
	sink.write((const char*)&header,sizeof(header));
}

void TarWriter::appendSymLink(const std::string& filepath, const std::string& linkTarget){
	if(ended)
		throw std::runtime_error("Cannot append to an ended tar stream");
	header_posix_ustar symEntry(filepath,0,header_posix_ustar::SymLink);
	if(linkTarget.size()>100)
		throw std::length_error("Specified link target longer than 100 characters.");
	std::copy(linkTarget.begin(),linkTarget.end(),symEntry.linkname);
	symEntry.setChecksum();
	sink.write((const char*)&symEntry,sizeof(header_posix_ustar));
}

void TarWriter::endStream(){
	if(!ended){
		for(unsigned int i=0; i<2*sizeof(header_posix_ustar); i++)
			sink.put('\0');
	}
}

void recursivelyArchive(std::string basePath, TarWriter& writer, bool stripPrefix){
	std::size_t prefixLen=0;
	if(stripPrefix){
		//figure out what prefix, if any, basePath has
		auto idx=basePath.rfind('/');
		if(idx!=std::string::npos)
			prefixLen=idx+1;
	}

	//strip leading slashes from paths going into the archive
	auto cleanPath=[=](std::string path)->std::string{
		if(stripPrefix)
			return path.substr(prefixLen);
		if(!path.empty() && path.front()=='/')
			return path.substr(1);
		return path;
	};

	std::queue<std::string> todo;
	todo.push(basePath);
	const directory_iterator end;
	while(!todo.empty()){
		std::string path=todo.front();
		todo.pop();
		//std::cout << "Processing directory " << path << std::endl;
				
		directory_iterator it(path);
		if(!it)
			throw std::runtime_error(path+" is not a readable directory");
			
		writer.appendDirectory(cleanPath(path));
		for(; it!=end; it++){
			if(it->path().name()=="." || it->path().name()=="..")
				continue;
			//std::cout << " Found " << it->path().str() << std::endl;
			if(is_directory(*it))
				todo.push(it->path().str());
			else if(is_regular_file(*it)){
				//TODO: get file contents!
				std::ifstream infile(it->path().str());
				if(!infile)
					throw std::runtime_error("Unable to open "+it->path().str()+" for reading");
				std::istreambuf_iterator<char> input(infile), eof;
				std::ostringstream fileData;
				std::copy(input, eof, std::ostreambuf_iterator<char>(fileData));
				writer.appendFile(cleanPath(it->path().str()),fileData.str());
			}
			else if(is_symlink(*it)){
				const size_t len=2048;
				std::unique_ptr<char[]> buf(new char[len]);
				ssize_t res=readlink(it->path().str().c_str(), buf.get(), len-1);
				if(res==-1)
					throw std::runtime_error("readlink failed on "+it->path().str());
				buf[len]=0;
				//std::cout << "  Link refers to to " << buf.get() << std::endl;
				writer.appendSymLink(cleanPath(it->path().str()),buf.get());
			}
		}
	}
}

