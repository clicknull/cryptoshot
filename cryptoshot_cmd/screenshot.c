/*
Author: DiabloHorn http://diablohorn.wordpress.com
Project: cryptoshot, taking enrypted screenshots
keywords: rsa, aes, dual monitor, screenshot

Resources used during the development proces:

Taking a screenshot & multiple monitors:
http://msdn.microsoft.com/en-us/library/windows/desktop/dd183402(v=vs.85).aspx
http://www.codeproject.com/Articles/101272/Creation-of-Multi-monitor-Screenshots-Using-WinAPI
http://www.codeproject.com/Articles/2522/Multiple-Monitor-Support#xx223852xx
http://stackoverflow.com/questions/3291167/how-to-make-screen-screenshot-with-win32-in-c
	Use GetDC(NULL); to get a DC for the entire screen.
	Use CreateCompatibleDC to get a compatible DC.
	Use CreateCompatibleBitmap to create a bitmap to hold the result.
	Use SelectObject to select the bitmap into the compatible DC.
	Use BitBlt to copy from the screen DC to the compatible DC.
	Deselect the bitmap from the compatible DC.
	When you create the compatible bitmap, you want it compatible with the screen DC, not the compatible DC.

Working with encryption
https://polarssl.org/kb/compiling-and-building/using-polarssl-in-microsoft-visual-studio-2010
https://polarssl.org/discussions/generic/how-to-read-an-openssl-generated-pem-txt-file
http://stackoverflow.com/questions/1231178/load-an-x509-pem-file-into-windows-cryptoapi
http://stackoverflow.com/questions/10212515/pycrypto-encrypt-an-string-twice-using-rsa-and-pkcs1
http://stackoverflow.com/questions/11505547/how-calculate-size-of-rsa-cipher-text-using-key-size-clear-text-length

Producint a smaller exe (not used yet in this project configuration)
Use at your own risk
http://thelegendofrandom.com/blog/archives/2231
*/
#include <Windows.h>
#include <stdio.h>
#include "polarssl/pk.h"
#include "polarssl/rsa.h"
#include "polarssl/entropy.h"
#include "polarssl/ctr_drbg.h"
#include "polarssl/aes.h"

//set to 1 for OutputDebugString usage instead of file output
#define GENERATE_OUTPUT 1
#define OUTPUT_LEVEL 1
//error level output, you know the drill
#define DBG_INFO 1
#define DBG_WARNING 2
#define DBG_ERROR 3

#define WIN32_LEAN_AND_MEAN


/*
	prints out messages to file
	It being error messages, thus unencrypted be careful
*/

void outputerror(int dbglevel,const char *format,...){
#if(GENERATE_OUTPUT == 1)
	int outputstringsize = 1024;
	int pos = 0;
	char outputstring[1024] = {0}; //should be more then enough
	va_list args = NULL;
	HANDLE hFile = NULL;
	DWORD dwbyteswritten = 0;

	hFile = CreateFile("info.output", FILE_GENERIC_WRITE | FILE_GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_READ, NULL,OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	SetFilePointer(hFile,0,0,FILE_END);
	va_start (args, format);
	switch(dbglevel){
		case DBG_INFO:
			pos = sprintf_s(outputstring,outputstringsize,"%s ","::INFO::");
			break;
		case DBG_WARNING:
			pos = sprintf_s(outputstring,outputstringsize,"%s ","::WARNING::");
			break;
		case DBG_ERROR:
			pos = sprintf_s(outputstring,outputstringsize,"%s ","::ERROR::");
			break;
	}
	vsprintf_s((outputstring+pos),outputstringsize,format,args);
	if (pos == -1){
		sprintf_s(outputstring,outputstringsize,"%s %s:%s\n","::ERROR::","vsprintf_s failed due to format string or null pointers",format);
		WriteFile(hFile,outputstring,1024,&dwbyteswritten,NULL);
	}
	va_end (args);
	
	if(dbglevel >= OUTPUT_LEVEL){
		WriteFile(hFile,outputstring,strlen(outputstring),&dwbyteswritten,NULL);
	}
	CloseHandle(hFile);
#endif	
}


/*
	Retrieves the public key from itself, layout on disk:
	[exe file data][public key(string)][public key size(int)]
	
*/
unsigned char *getpublickeyfromself(const char *filename,int *keylen){
	HANDLE openedfile = NULL;
	int filesize = 0;
	BOOL fileread = FALSE;
	unsigned char *publickey = NULL;
	int publickeysize = {0};
	DWORD bytesread = 0;
	DWORD setfilepointerresult = 0;
	BOOL readfileresult = FALSE;

	openedfile = CreateFile(filename,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
	if(openedfile == INVALID_HANDLE_VALUE){
		outputerror(DBG_ERROR,"%s\n","getpublickeyfromself::failed to open myself");
		return NULL;
	}

	filesize = GetFileSize(openedfile,NULL);
	if(filesize == INVALID_FILE_SIZE){
		return NULL;
	}

	//read the size of the public key data we want
	setfilepointerresult = SetFilePointer(openedfile,-4,NULL,FILE_END);
	if(setfilepointerresult == INVALID_SET_FILE_POINTER){
		outputerror(DBG_ERROR,"%s\n","getpublickeyfromself::could not set filepointer to beginning of int size");
		CloseHandle(openedfile);
		return NULL;
	}

	readfileresult = ReadFile(openedfile,&publickeysize,4,&bytesread,NULL);
	if(readfileresult == FALSE){
		outputerror(DBG_ERROR,"%s\n","getpublickeyfromself::could not read myself");
		CloseHandle(openedfile);
		return NULL;
	}
	//reset filepointer
	setfilepointerresult = 0;
	setfilepointerresult = SetFilePointer(openedfile,-4,NULL,FILE_END);
	if(setfilepointerresult == INVALID_SET_FILE_POINTER){
		outputerror(DBG_ERROR,"%s\n","getpublickeyfromself::could not reset filepointer to previous position");
		CloseHandle(openedfile);
		return NULL;
	}
	//account for nullbyte
	publickeysize = publickeysize+1;
	publickey = (unsigned char *)malloc(publickeysize);
	SecureZeroMemory(publickey,publickeysize);
	//set filepointer to beginning of public key data
	setfilepointerresult = 0;
	setfilepointerresult = SetFilePointer(openedfile,-(publickeysize-1),NULL,FILE_CURRENT);
	if(setfilepointerresult == INVALID_SET_FILE_POINTER){
		outputerror(DBG_ERROR,"%s\n","getpublickeyfromself::could not set pointer to beginning of public key data");
		CloseHandle(openedfile);
		SecureZeroMemory(publickey,publickeysize);
		free(publickey);
		return NULL;
	}
	readfileresult = FALSE;
	readfileresult = ReadFile(openedfile,publickey,publickeysize-1,&bytesread,NULL);
	if(readfileresult == FALSE){
		outputerror(DBG_ERROR,"%s\n","getpublickeyfromself::could not read public key data");
		CloseHandle(openedfile);
		SecureZeroMemory(publickey,publickeysize);
		free(publickey);
		return NULL;
	}
	CloseHandle(openedfile);
	*keylen = publickeysize;
	return publickey;
}

/*
	Takes a screenshot of the screen and saves it in memory
*/
int takescreenshot(unsigned char **screenshotbuffer,int *screenshotbuffersize){
	//declaring & initializing needed vars
	HDC screendc = NULL;
	HDC compatiblescreendc = NULL;
	HBITMAP compatiblebitmap = NULL;
	HGDIOBJ selectedobject = NULL;
	BOOL bitbltresult = FALSE;
	int getobjectresult = 0;
	BITMAP finalbmp = {0};
	BITMAPFILEHEADER bmfileheader = {0};    
	BITMAPINFOHEADER bminfoheader = {0};
	DWORD dwBmpSize = 0;
	HANDLE hDIB = NULL;
	unsigned char *lpbitmap = NULL;
	int getdibitsresult = 0;	
	DWORD dwSizeofDIB = 0;
	int screenwidth = 0;
	int screenheight = 0;
	int leftxscreenpos = 0;
	int leftyscreenpos = 0;	
	char currentpath[MAX_PATH] = {0};

	//width in pixels of the virtual screen
	leftxscreenpos = GetSystemMetrics(SM_XVIRTUALSCREEN);
	//height in pixels of the virtual screen
	leftyscreenpos = GetSystemMetrics(SM_YVIRTUALSCREEN);
	//left side virtual screen coordinates
	screenwidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	//top side virtual screen coordinates
	screenheight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
	/*actually take the screenshot*/
	screendc = GetDC(NULL); 
	if(screendc == NULL){
		outputerror(DBG_ERROR,"%s\n","takescreenshot::GetDC() Failed");
		return 1;
	}
	compatiblescreendc = CreateCompatibleDC(screendc);
	if(compatiblescreendc == NULL){
		outputerror(DBG_ERROR,"%s\n","takescreenshot::CreateCompatibleDC() Failed");
		ReleaseDC(NULL,screendc);
		return 1;
	}
	compatiblebitmap = CreateCompatibleBitmap(screendc,screenwidth,screenheight);
	if(compatiblebitmap == NULL){
		outputerror(DBG_ERROR,"%s\n","takescreenshot::CreateCompatibleBitmap() Failed");
		ReleaseDC(NULL,screendc);
		DeleteDC(compatiblescreendc);
		return 1;
	}
	selectedobject = SelectObject(compatiblescreendc,compatiblebitmap);
	if(selectedobject == NULL || selectedobject == HGDI_ERROR){
		outputerror(DBG_ERROR,"%s\n","takescreenshot::SelectObject() Failed");
		ReleaseDC(NULL,screendc);
		DeleteDC(compatiblescreendc);
		DeleteObject(compatiblebitmap);
		return 1;
	}
	bitbltresult = BitBlt(compatiblescreendc,0,0,screenwidth,screenheight,screendc,leftxscreenpos,leftyscreenpos,SRCCOPY);
	if(bitbltresult == 0){
		outputerror(DBG_ERROR,"%s %d\n","takescreenshot::BitBlt() Failed", GetLastError());
		ReleaseDC(NULL,screendc);
		DeleteDC(compatiblescreendc);
		DeleteObject(compatiblebitmap);		
		return 1;
	}
	/*save the screenshot to file*/
	getobjectresult = GetObject(compatiblebitmap,sizeof(BITMAP),&finalbmp);
	if(getobjectresult == 0){
		outputerror(DBG_ERROR,"%s\n","takescreenshot::GetObject() Failed");
		ReleaseDC(NULL,screendc);
		DeleteDC(compatiblescreendc);
		DeleteObject(compatiblebitmap);
		return 1;
	}

	//bmp file format good read: http://en.wikipedia.org/wiki/BMP_file_format
	bminfoheader.biSize = sizeof(BITMAPINFOHEADER);    
    bminfoheader.biWidth = screenwidth;    
    bminfoheader.biHeight = screenheight;  
    bminfoheader.biPlanes = 1;    
    bminfoheader.biBitCount = 32;    
    bminfoheader.biCompression = BI_RGB;    
    bminfoheader.biSizeImage = 0;  
    bminfoheader.biXPelsPerMeter = 0;    
    bminfoheader.biYPelsPerMeter = 0;    
    bminfoheader.biClrUsed = 0;    
    bminfoheader.biClrImportant = 0;

	dwBmpSize = ((screenwidth * bminfoheader.biBitCount + 31) / 32) * 4 * screenheight;

	hDIB = GlobalAlloc(GHND,dwBmpSize); 
    lpbitmap = (unsigned char *)GlobalLock(hDIB);  
	//get the actual bitmap 'bits'
	getdibitsresult = GetDIBits(compatiblescreendc, compatiblebitmap, 0,(UINT)finalbmp.bmHeight, lpbitmap, (BITMAPINFO *)&bminfoheader, DIB_RGB_COLORS);
	if(getdibitsresult == 0){
		outputerror(DBG_ERROR,"%s\n","takescreenshot::GetDIBits() Failed");
		ReleaseDC(NULL,screendc);
		DeleteDC(compatiblescreendc);
		DeleteObject(compatiblebitmap);
		SecureZeroMemory(lpbitmap,dwBmpSize);
		GlobalUnlock(hDIB); 
		return 1;
	}

    dwSizeofDIB = dwBmpSize + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bmfileheader.bfOffBits = (DWORD)sizeof(BITMAPFILEHEADER) + (DWORD)sizeof(BITMAPINFOHEADER); 
    bmfileheader.bfSize = dwSizeofDIB; 
    bmfileheader.bfType = 0x4D42;	

	outputerror(DBG_INFO,"%s\n","takescreenshot::screenshot taken, preparing memory file");
	*screenshotbuffersize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dwBmpSize;
	outputerror(DBG_INFO,"%s %i\n","takescreenshot::memfile size",*screenshotbuffersize);
	*screenshotbuffer = (unsigned char *)malloc(*screenshotbuffersize);
	if(screenshotbuffer == NULL){
		Sleep(10000);// 10 seconds
		*screenshotbuffer = (char *)malloc(*screenshotbuffersize);
		if(screenshotbuffer == NULL){
			outputerror(DBG_ERROR,"%s\n","takescreenshot::malloc() final file failed");
			ReleaseDC(NULL,screendc);
			DeleteDC(compatiblescreendc);
			DeleteObject(compatiblebitmap);
			SecureZeroMemory(lpbitmap,dwBmpSize);
			GlobalUnlock(hDIB);
			return 1;
		}
	}
	outputerror(DBG_INFO,"%s\n","takescreenshot::memfile prepared, copy bytes");
	/* create the full file in memory */
	memcpy_s(*screenshotbuffer,*screenshotbuffersize,&bmfileheader,sizeof(BITMAPFILEHEADER));
	memcpy_s(*screenshotbuffer+sizeof(BITMAPFILEHEADER),*screenshotbuffersize,&bminfoheader,sizeof(BITMAPINFOHEADER));
	memcpy_s(*screenshotbuffer+sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER),*screenshotbuffersize,lpbitmap,dwBmpSize);
			
	/* we could have used more of these in this app */
	SecureZeroMemory(lpbitmap,dwBmpSize);
	SecureZeroMemory(&bmfileheader,sizeof(BITMAPFILEHEADER));
	SecureZeroMemory(&bminfoheader,sizeof(BITMAPINFOHEADER));
	/*release resources*/
	GlobalUnlock(hDIB);
	ReleaseDC(NULL,screendc);
	DeleteDC(compatiblescreendc);
	DeleteObject(compatiblebitmap);
	GlobalFree(hDIB);
	return 0;
}

/*
	shameless copy/paste from:
	https://polarssl.org/kb/how-to/generate-an-aes-key
*/
unsigned char *generatekey(char *pers, int size){
	ctr_drbg_context ctr_drbg = {0};
	entropy_context entropy = {0};
	int keysize = 0;
	unsigned char *key = NULL;	
	int ret = 0;

	//convert to bytes
	keysize = size / 8;

	entropy_init( &entropy );
	if((ret = ctr_drbg_init(&ctr_drbg, entropy_func, &entropy, (unsigned char *)pers,strlen(pers))) != 0 ){
		outputerror(DBG_ERROR,"%s\n","generatekey::failed to initialize random generator");
		return NULL;
	}
		
	key = (unsigned char *)malloc(keysize);
	if(key == NULL){
		outputerror(DBG_ERROR,"%s\n","generatekey::failed to malloc");
		return NULL;
	}
	
	if((ret = ctr_drbg_random(&ctr_drbg,key,keysize)) != 0 ){
		outputerror(DBG_ERROR,"%s\n","generatekey::failed to produce random data");
		return NULL;
	}

	entropy_free(&entropy);
	return key;
}

/*
	shameless adjustment from:
	https://polarssl.org/kb/how-to/encrypt-with-aes-cbc
	not the most efficient one
*/
unsigned char *encryptaes(unsigned char *key, unsigned int keysize, unsigned char *iv, unsigned char *inputdata, int inputdatalen, int *outputdatalen){
	aes_context aes_ctx = {0};
	unsigned char *inputdatapadded = NULL;
	unsigned char *encrypteddata = NULL;
	int inputdatapaddedlen = 0;	

	inputdatapaddedlen = inputdatalen+(16-(inputdatalen % 16));
	*outputdatalen = inputdatapaddedlen;
	//allocate enough space
	inputdatapadded = (unsigned char *)malloc(inputdatapaddedlen);
	encrypteddata = (unsigned char *)malloc(inputdatapaddedlen);
	SecureZeroMemory(inputdatapadded,inputdatapaddedlen);
	SecureZeroMemory(encrypteddata,inputdatapaddedlen);

	//setup the data that will eventually be encrypted
	memcpy_s(inputdatapadded,inputdatapaddedlen,inputdata,inputdatalen);
	//set key
	if(aes_setkey_enc(&aes_ctx, key, keysize) != 0){
		free(encrypteddata);
		free(inputdatapadded);
		return NULL;
	}
	//encrypt
	if(aes_crypt_cbc(&aes_ctx,AES_ENCRYPT,inputdatapaddedlen,iv,inputdatapadded,encrypteddata) != 0){
		SecureZeroMemory(inputdatapadded,inputdatapaddedlen);
		SecureZeroMemory(encrypteddata,inputdatapaddedlen);
		free(encrypteddata);
		free(inputdatapadded);
		return NULL;
	}

	//free resources
	SecureZeroMemory(inputdatapadded,inputdatapaddedlen);
	free(inputdatapadded);

	return encrypteddata;

}

/*
	get context to public key by parsing it
*/
pk_context getpubkeycontext(const unsigned char *rsapublickey, int rsapublickeylen){
	pk_context pkctx = {0};
	int pkresult = 0;

	pk_init(&pkctx);
	pkresult = pk_parse_public_key(&pkctx,rsapublickey,rsapublickeylen);
	if(pkresult != 0){
		outputerror(DBG_ERROR,"%s\n","getpubkeycontext::failed to parse public key");
		return pkctx;
	}	

	pkresult = 0;
	pkresult = pk_can_do(&pkctx,POLARSSL_PK_RSA);
	if(pkresult != 1){
		outputerror(DBG_ERROR,"%s\n","getpubkeycontext::key does not support RSA operations");
		return pkctx;
	}
	
	return pkctx;
}
/*
	rsa pkcs1 v1.5 encryption
*/
unsigned char *rsacrypt(pk_context *pkctx,const unsigned char *plaintext,const unsigned int plaintextsize){
	entropy_context entropy = {0};
	ctr_drbg_context ctr_drbg = {0};	
	rsa_context rsactx = {0};
	int pkresult = 0;
	unsigned char *encryptedoutput = NULL;
	unsigned int encryptedoutputsize = 0;
	char pers[33] = "3s:!2OXI(FX%#Q($[CEjiGRIk\\-)4e&?";
	int ret = 0;
	
	entropy_init( &entropy );
	if((ret = ctr_drbg_init(&ctr_drbg, entropy_func, &entropy, (unsigned char *)&pers[0],strlen(pers))) != 0 ){
		outputerror(DBG_ERROR,"%s\n","rsacrypt::failed to initialize random generator");
		return NULL;
	}


	encryptedoutputsize = pk_get_len(pkctx);
	outputerror(DBG_INFO,"%s %Iu\n","rsacrypt::buffer size for rsa encrypted output ",encryptedoutputsize);
	encryptedoutput = (unsigned char *)malloc(encryptedoutputsize);
	SecureZeroMemory(encryptedoutput,encryptedoutputsize);		
	rsa_copy(&rsactx,pkctx->pk_ctx);
	rsactx.padding = RSA_PKCS_V21;
	rsactx.hash_id = POLARSSL_MD_SHA1;	
	pkresult = 0;		
	pkresult = rsa_rsaes_oaep_encrypt(&rsactx,ctr_drbg_random,&ctr_drbg,RSA_PUBLIC,"cryptoshot",strlen("cryptoshot"),plaintextsize,plaintext,encryptedoutput);
	if(pkresult != 0){
		outputerror(DBG_ERROR,"%s %i\n","rsacrypt::failed to encrypt data",pkresult);
		return NULL;
	}

	entropy_free(&entropy);	
	rsa_free(&rsactx);
	return encryptedoutput;
}

/*
	main logic, output file format:
	[length encrypted key data(int)][encrypted key data][encrypted bmp data]
	where key daya = [aes key][aes iv]
*/
int main(int argc, char *argv[]){
	//misc vars
	char currentpath[MAX_PATH] = {0};
	//vars for getting public key from exe
	unsigned char *pubrsakey = NULL;
	int pubkeylen = 0;	
	//vars for taking the screenshot
	unsigned char *finalbmpfile = NULL;
	int finalbmpfilesize = 0;
	//vars for data encryption
	pk_context pk_ctx;
	char *keypersonalisation = "5T+qDlP1=R1ek?GLqi|=)1O(niSimHBx|2\5QE.DN<7W\"]I@:?uSa#}txXN<9oG6";
	char *ivpersonalisation = "J0eeYYCW.`6m;I5[v4|]0NDe1Hx)Co8D u]~9ZC\"x6AESc=a\\/W-e7d1bnMwq,z=]";	
	unsigned char *aeskey = NULL;
	unsigned char *aesiv = NULL;
	unsigned char *encrypteddata = NULL;
	int encrypteddatalen = 0;
	unsigned char *pubkeyencrypteddata;
	unsigned int pubkeyencrypteddatalen = 0;
	unsigned char keydata[48] = {0};
	//vars for writing to file
	DWORD dwBytesWritten = 0;
	HANDLE hFile = NULL;
	
	outputerror(DBG_INFO,"%s\n","main::started");
	/* get public key*/
	GetModuleFileName(NULL,&currentpath[0],sizeof(currentpath));
	pubrsakey = getpublickeyfromself(&currentpath[0],&pubkeylen);
	if(pubrsakey == NULL){
		outputerror(DBG_ERROR,"%s\n","main::failed to get public key");
		SecureZeroMemory(currentpath,(sizeof(currentpath)/sizeof(currentpath[0])));
		exit(1);
	}

	SecureZeroMemory(currentpath,(sizeof(currentpath)/sizeof(currentpath[0])));
	/* take screenshot */
	if(takescreenshot(&finalbmpfile,&finalbmpfilesize) == 1){
		outputerror(DBG_ERROR,"%s\n","main::failed to take screenshot");
		SecureZeroMemory(finalbmpfile,finalbmpfilesize);
		SecureZeroMemory(currentpath,(sizeof(currentpath)/sizeof(currentpath[0])));
		free(finalbmpfile);
		exit(1);
	}

	/* generate keys, encrypt keys, encrypt screenshot, save screenshot */
	aeskey = generatekey(keypersonalisation,256);
	aesiv = generatekey(ivpersonalisation,128);
	memcpy_s(keydata,48,aeskey,32);
	memcpy_s(keydata+32,48,aesiv,16);

	/* get and parse public key */
	pk_ctx = getpubkeycontext(pubrsakey,pubkeylen);
	if(pk_get_len(&pk_ctx) == 0){
		outputerror(DBG_ERROR,"%s\n","main::failed to parse public key");
		pk_free(&pk_ctx);
		SecureZeroMemory(finalbmpfile,finalbmpfilesize);
		SecureZeroMemory(currentpath,(sizeof(currentpath)/sizeof(currentpath[0])));
		free(finalbmpfile);
		exit(1);
	}
	/* encrypt aes key and iv */
	pubkeyencrypteddatalen = pk_get_len(&pk_ctx);
	pubkeyencrypteddata = (unsigned char *)malloc(pubkeyencrypteddatalen);
	SecureZeroMemory(pubkeyencrypteddata,pubkeyencrypteddatalen);
	pubkeyencrypteddata = rsacrypt(&pk_ctx,keydata,48);
	if(pubkeyencrypteddata == NULL){
		outputerror(DBG_ERROR,"%s\n","main::failed to encrypt aes key + aes iv");
		pk_free(&pk_ctx);
		SecureZeroMemory(aeskey,32);
		SecureZeroMemory(aesiv,16);
		SecureZeroMemory(finalbmpfile,finalbmpfilesize);
		SecureZeroMemory(currentpath,(sizeof(currentpath)/sizeof(currentpath[0])));
		exit(1);
	}
	hFile = CreateFile("screen.enc", GENERIC_WRITE, 0, NULL,CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	WriteFile(hFile,(char *)&pubkeyencrypteddatalen,4,&dwBytesWritten,NULL);
	WriteFile(hFile,pubkeyencrypteddata,pubkeyencrypteddatalen,&dwBytesWritten,NULL);
	//encrypt and save screenshot
	encrypteddata = encryptaes(aeskey,256,aesiv,finalbmpfile,finalbmpfilesize,&encrypteddatalen);
	if(encrypteddata == NULL){
		outputerror(DBG_ERROR,"%s\n","main::failed to encrypt the actual screenshot");
		pk_free(&pk_ctx);
		SecureZeroMemory(finalbmpfile,finalbmpfilesize);
		SecureZeroMemory(currentpath,(sizeof(currentpath)/sizeof(currentpath[0])));
		free(finalbmpfile);
		exit(1);
	}
	/* save screenshot */
	WriteFile(hFile,encrypteddata,encrypteddatalen,&dwBytesWritten,NULL);
	CloseHandle(hFile);

	/* cleanup */	
	pk_free(&pk_ctx);
	SecureZeroMemory(finalbmpfile,finalbmpfilesize);
	SecureZeroMemory(aeskey,32);
	SecureZeroMemory(aesiv,16);
	SecureZeroMemory(finalbmpfile,finalbmpfilesize);
	free(finalbmpfile);
	free(pubrsakey);
	outputerror(DBG_INFO,"%s\n","main::finished");
	return 0;
}