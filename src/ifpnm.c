/*
**  PnM to DIB filter for Susie32
**
**  Public domain by MIYASAKA Masaru (Apr 17, 2006)
*/

#define STRICT			/* enable strict type checking */
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include "spibase.h"

#define IFPNM_VERSION	"0.14"		/* �v���O�C���o�[�W���� */

#define IsDigit(c)		('0'<=(c) && (c)<='9')
#define IsSpace(c)		((c)=='\n' || (c)==' ' || (c)=='\t' || (c)=='\r')

	/* typedefs */
typedef struct pnm PNM;
#ifdef	__GNUC__
typedef int FASTCALL (*READFNC)(PNM *, LPBYTE);
typedef UINT FASTCALL (*GETBFNC)(SPI_FILE *, int);
typedef DWORD FASTCALL (*GETVFNC)(PNM *);
#else
typedef int (FASTCALL *READFNC)(PNM *, LPBYTE);
typedef UINT (FASTCALL *GETBFNC)(SPI_FILE *, int);
typedef DWORD (FASTCALL *GETVFNC)(PNM *);
#endif

	/* pnm �\���� */
struct pnm {
	SPI_FILE *fp;			/* ���̓t�@�C�� */
	DWORD   width;			/* �摜�̕� */
	DWORD   height;			/* �摜�̍��� */
	DWORD   maxval;			/* �ő�T���v���l */
	int     maxpal;			/* �ő�p���b�g�C���f�N�X�l */
	int     palettes;		/* �p���b�g�� */
	int     pixdepth;		/* �s�N�Z���[�x */
	HLOCAL  comment;		/* �R�����g�e�L�X�g(�������n���h��) */
	/* ---- */
	READFNC ReadRow;		/* �X�L�������C���ǂݍ��݊֐� */
	GETBFNC GetBits;		/* �r�b�g�ǂݍ��݊֐�(PBM) */
	GETVFNC GetVal;			/* �T���v���l�ǂݍ��݊֐�(PGM/PPM) */
};

	/* �v���g�^�C�v�錾 */
static int FASTCALL ReadHeader(PNM *, BOOL);
static int FASTCALL GetByte(PNM *);
static int FASTCALL GetIoErrCode(PNM *);
static void FASTCALL BuildPalette(PNM *, RGBQUAD *);
static int FASTCALL ReadRowPbm(PNM *, LPBYTE);
static int FASTCALL ReadRowPgm(PNM *, LPBYTE);
static int FASTCALL ReadRowPg4(PNM *, LPBYTE);
static int FASTCALL ReadRowPpm(PNM *, LPBYTE);
static UINT FASTCALL GetAscBits(SPI_FILE *, int);
static UINT FASTCALL GetRawBits(SPI_FILE *, int);
static DWORD FASTCALL GetAscValE(PNM *);
static DWORD FASTCALL GetAscValN(PNM *);
static DWORD FASTCALL GetRawValE(PNM *);
static DWORD FASTCALL GetRawValL(PNM *);
static DWORD FASTCALL GetRawValG(PNM *);



/* ---------------------------------------------------------------------
**		���C������
*/

	/* �O���[�o���ϐ� */
const int NumInfo = 4;			/* �v���O�C�����̏�� */
const LPCSTR PluginInfo[] = {	/* �v���O�C����� */
	"00IN",
	"PnM to DIB filter ver." IFPNM_VERSION " by Miyasaka, Masaru",
	"*.pbm;*.pgm;*.ppm;*.pnm",
	"pbmplus(plain/raw)"
};


/*
**		.pnm �t�@�C�����ǂ����`�F�b�N����
*/
int IsSupportedFormat(LPBYTE buf, DWORD rbytes, LPSTR filename)
{
	if (rbytes < 6) return FALSE;	/* strlen("P1 0 0"); */

	/* regular expression : "P[1-6][ \t\r\n#]" */

	if (buf[0] != 'P') return FALSE;
	if (buf[1] < '1' || '6' < buf[1]) return FALSE;
	if (!IsSpace(buf[2]) && buf[2] != '#') return FALSE;

	return TRUE;
}


/*
**		.pnm �t�@�C���̊e����𓾂�
*/
int GetImageInfo(SPI_FILE *fp, PictureInfo *lpInfo)
{
	PNM pnm;
	int err;

	pnm.fp = fp;
	err = ReadHeader(&pnm, TRUE);

	if (err == SPI_ERROR_SUCCESS)
		SpiSetPictureInfo(lpInfo, pnm.width, pnm.height, pnm.pixdepth,
		                  0, 0, 0, 0, pnm.comment);

	return err;
}


/*
**		.pnm �t�@�C���̉摜��W�J����
*/
int GetImage(SPI_FILE *fp, HANDLE *pHBInfo, HANDLE *pHBImg,
             SPIPROC lpProgCallback, long lData)
{
	enum { NCALL = 120 };	/* �R�[���o�b�N�֐����Ăԉ� */
	PNM pnm;
	LPBITMAPINFO lpbmi;
	LPBYTE lpbits, lprow;
	DWORD rowno, rowbytes;
	int prog, prev, err;

	pnm.fp = fp;
	err = ReadHeader(&pnm, FALSE);
	if (err != SPI_ERROR_SUCCESS) return err;

	err = SpiInitBitmap(pHBInfo, &lpbmi, pHBImg, &lpbits, &rowbytes, pnm.width,
	                    pnm.height, pnm.pixdepth, pnm.palettes, 0, 0);
	if (err != SPI_ERROR_SUCCESS) return err;

	BuildPalette(&pnm, lpbmi->bmiColors);

	lprow = lpbits + rowbytes * pnm.height;
	prev = -1;
	for (rowno = 0; ; rowno++) {
		if (lpProgCallback != NULL &&
		    (prog = rowno * NCALL / pnm.height) != prev &&
		    lpProgCallback((prev = prog), NCALL, lData)) {
			err = SPI_ERROR_CANCEL_EXPAND;
			break;
		}
		if (rowno >= pnm.height) break;
			/* -- */
		err = pnm.ReadRow(&pnm, (lprow -= rowbytes));
		if (err != SPI_ERROR_SUCCESS) break;
	}
	/*
	 *	�u�t�@�C���I�[�v�G���[�ɂȂ�t�@�C�����A�\�Ȍ���\������
	 *	�悤�ɂ���ɂ́A���̕��̃R�����g���O���Ă��������B
	 */
/*	if (err == SPI_ERROR_END_OF_FILE) err = SPI_ERROR_SUCCESS;*/

	if (err == SPI_ERROR_SUCCESS) {
		SpiUnlockBuffer(pHBInfo);
		SpiUnlockBuffer(pHBImg);
	} else {
		SpiFreeBuffer(pHBInfo);
		SpiFreeBuffer(pHBImg);
	}

	return err;
}


/* ---------------------------------------------------------------------
**		PnM �t�@�C������(Part.1) - �w�b�_
*/

/*
**		PnM �t�@�C���w�b�_��ǂ�
*/
static int FASTCALL
 ReadHeader(PNM *lpPnm, BOOL bGetComment)
{
	enum { PBM = 0, PGM = 1, PPM = 2, PG4 = 3 };
	enum { GROW_SIZE = 256 };
		/* ---- */
	static const int  palettes[] = { 2, 256, 0, 16 };
	static const BYTE pixdepth[] = { 1,  8, 24,  4 };
	static const READFNC ReadRow[] = {
		ReadRowPbm, ReadRowPgm, ReadRowPpm, ReadRowPg4
	};
	static const GETBFNC GetBits[] = {
		GetAscBits, GetRawBits
	};
	static const GETVFNC GetVal[][3] = {
		{ GetAscValE, GetAscValN, GetAscValN },
		{ GetRawValE, GetRawValL, GetRawValG }
	};
		/* ---- */
	BYTE magic[3];
	int i, c, err, raw, type, vals;
	DWORD textlen, bufflen, val;
	LPBYTE textbuf;

	for (i = 0; i < 3; i++) magic[i] = GetByte(lpPnm);
	if ((err = GetIoErrCode(lpPnm)) != SPI_ERROR_SUCCESS) return err;
	if (!IsSupportedFormat(magic, 8, NULL)) return SPI_ERROR_UNKNOWN_FORMAT;

	raw = (magic[1] >= '4');
	type = magic[1] - (raw ? '4' : '1');
	vals = (type == PBM) ? 2 : 3;

	lpPnm->comment = NULL;
	textlen = bufflen = 0;
	c = magic[2];

	for (i = 0; i < vals; i++) {
		for ( ; c == '#' || IsSpace(c); c = GetByte(lpPnm)) {
			if (c != '#') continue;
			do {
				c = GetByte(lpPnm);
				if (bGetComment) {
					int n = textlen++;
					if (textlen >= bufflen) {
						err = SpiReAllocBuffer(&lpPnm->comment, &textbuf,
						                       (bufflen += GROW_SIZE));
						if (err != SPI_ERROR_SUCCESS) goto error;
					}
					textbuf[n] = c;
				}
			} while (c >= 0 && c != '\n');
		}
		val = 0;
		for ( ; IsDigit(c); c = GetByte(lpPnm)) {
			val *= 10;
			val += c - '0';
		}
		if ((err = GetIoErrCode(lpPnm)) != SPI_ERROR_SUCCESS) goto error;
		if (val == 0) { err = SPI_ERROR_BROKEN_DATA; goto error; }
		switch (i) {
		case 0:
			lpPnm->width  = val;
			break;
		case 1:
			lpPnm->height = val;
			break;
		case 2:
			if (raw && val > 0xFFFF) {
				err = SPI_ERROR_BROKEN_DATA; goto error;
			}
			lpPnm->maxval = val;
			break;
		}
	}
	if (lpPnm->comment != NULL) {
		textbuf[textlen++] = '\0';
		SpiReAllocBuffer(&lpPnm->comment, NULL, textlen);
	}

	if (type == PGM && lpPnm->maxval < 255) {
		lpPnm->maxpal = lpPnm->maxval;	/* �p���b�g�̕����X�P�[�����O���� */
		lpPnm->maxval = 255;		/* �T���v���l�̃X�P�[�����O�𖳌��ɂ��� */
		if (lpPnm->maxpal <= 15) type = PG4;		/* 16�F�`���ŏo�͂ł��� */
	} else {
		lpPnm->maxpal = palettes[type] - 1;
	}
	lpPnm->palettes = palettes[type];
	lpPnm->pixdepth = pixdepth[type];
	lpPnm->ReadRow = ReadRow[type];
	lpPnm->GetBits = GetBits[raw];
	lpPnm->GetVal = GetVal[raw][(lpPnm->maxval!=255) + (lpPnm->maxval>255)];

	return SPI_ERROR_SUCCESS;

error:
	SpiFreeBuffer(&lpPnm->comment);
	return err;
}


/*
**		1�o�C�g��ǂ�
*/
static int FASTCALL
 GetByte(PNM *lpPnm)
{
	return SpiGetByte(lpPnm->fp);
}


/*
**		I/O �֌W�̃G���[�R�[�h�𓾂�
*/
static int FASTCALL
 GetIoErrCode(PNM *lpPnm)
{
	if (SpiIsEOF(lpPnm->fp)) return SPI_ERROR_END_OF_FILE;
	if (SpiIsError(lpPnm->fp)) return SPI_ERROR_FILE_READ;
	return SPI_ERROR_SUCCESS;
}


/*
**		�p���b�g�𐶐�����
*/
static void FASTCALL
 BuildPalette(PNM *lpPnm, RGBQUAD *lpColors)
{
	int max = lpPnm->maxpal;
	int val, i;

	for (i = 0; i <= max; i++) {
		val = (255 * i + max/2) / max;
		lpColors->rgbBlue  = val;
		lpColors->rgbGreen = val;
		lpColors->rgbRed   = val;
		lpColors++;
	}
}


/* ---------------------------------------------------------------------
**		PnM �t�@�C������(Part.2) - �摜�f�[�^
*/

/*
**		1�X�L�������C����ǂ� (PBM)
*/
static int FASTCALL
 ReadRowPbm(PNM *lpPnm, LPBYTE lpRow)
{
	static const BYTE mask[] = {
		0x00, 0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE
	};
	SPI_FILE *fp = lpPnm->fp;
	DWORD i;

	for (i = lpPnm->width; i >= 8; i -= 8)
		*lpRow++ = (BYTE)lpPnm->GetBits(fp, 8);
	if (i > 0)
		*lpRow   = (BYTE)lpPnm->GetBits(fp, i) & mask[i];

	return GetIoErrCode(lpPnm);
}


/*
**		�r�b�g�l(0/1)��ǂ� (Ascii)
*/
static UINT FASTCALL
 GetAscBits(SPI_FILE *fp, int num)
{
	UINT bits = 0;
	UINT c;
	int n;

	for (n = num; --n >= 0; ) {
		/*
		 *	��SpiGetByte() �́AEOF �� Error �̂Ƃ� (~0xFF|'0') ��Ԃ��̂ŁA
		 *	  ���[�v�𔲂����� (see spibase.h)�B
		 */
		while (c = SpiGetByte(fp), (c & 0xFE) != '0') ;	/* c!='0' && c!='1' */
		bits <<= 1; bits |= (c & 0x01);
	}
	if (num < 8) {
		bits <<= (8 - num);
	}
	return ~bits;
}


/*
**		�r�b�g�l(0/1)��ǂ� (Raw)
*/
static UINT FASTCALL
 GetRawBits(SPI_FILE *fp, int num)
{
	return ~SpiGetByte(fp);
}


/*
**		1�X�L�������C����ǂ� (PGM)
*/
static int FASTCALL
 ReadRowPgm(PNM *lpPnm, LPBYTE lpRow)
{
	DWORD i;

	for (i = lpPnm->width; i > 0; i--) {
		*lpRow++ = (BYTE)lpPnm->GetVal(lpPnm);
	}
	return GetIoErrCode(lpPnm);
}


/*
**		1�X�L�������C����ǂ� (PGM,4bit/pixel)
*/
static int FASTCALL
 ReadRowPg4(PNM *lpPnm, LPBYTE lpRow)
{
	DWORD i;
	BYTE b;

	for (i = lpPnm->width; i > 1; i -= 2) {
		b  = (BYTE)lpPnm->GetVal(lpPnm) << 4;
		b |= (BYTE)lpPnm->GetVal(lpPnm) & 0x0F;
		*lpRow++ = b;
	}
	if (i > 0) {
		b  = (BYTE)lpPnm->GetVal(lpPnm) << 4;
		*lpRow   = b;
	}
	return GetIoErrCode(lpPnm);
}


/*
**		1�X�L�������C����ǂ� (PPM)
*/
static int FASTCALL
 ReadRowPpm(PNM *lpPnm, LPBYTE lpRow)
{
	DWORD i;

	for (i = lpPnm->width; i > 0; i--) {
		lpRow[2] = (BYTE)lpPnm->GetVal(lpPnm);
		lpRow[1] = (BYTE)lpPnm->GetVal(lpPnm);
		lpRow[0] = (BYTE)lpPnm->GetVal(lpPnm);
		lpRow += 3;
	}
	return GetIoErrCode(lpPnm);
}


/*
**		�T���v���l��ǂ� (Ascii, maxval==255)
*/
static DWORD FASTCALL
 GetAscValE(PNM *lpPnm)
{
	SPI_FILE *fp = lpPnm->fp;
	DWORD val = 0;
	int c;

	while (c = SpiGetByte(fp), !IsDigit(c & 0xFF)) ;
	if (c < 0) return val;	/* EOF */

	do {
		val *= 10;
		val += c - '0';
	} while (c = SpiGetByte(fp), IsDigit(c));

	SpiClearEOF(fp);
	/*
	 *	���Ō�̐��l�̌�ɋ󔒕������Ȃ��ꍇ�AEOF �G���[��
	 *	  �Ȃ��Ă��܂��̂ŁA������ EOF �t���O���N���A����B
	 */
	return val;
}


/*
**		�T���v���l��ǂ� (Ascii, maxval!=255)
*/
static DWORD FASTCALL
 GetAscValN(PNM *lpPnm)
{
	DWORD max = lpPnm->maxval;
	return (255 * GetAscValE(lpPnm) + max/2) / max;
}


/*
**		�T���v���l��ǂ� (Raw, maxval==255)
*/
static DWORD FASTCALL
 GetRawValE(PNM *lpPnm)
{
	return SpiGetByte(lpPnm->fp);
}


/*
**		�T���v���l��ǂ� (Raw, maxval<255)
*/
static DWORD FASTCALL
 GetRawValL(PNM *lpPnm)
{
	SPI_FILE *fp = lpPnm->fp;
	DWORD max = lpPnm->maxval;
	DWORD val;

	val = SpiGetByte(fp);

		/* ���\�����ɂ���Ƃ����ƍ����ɂȂ�񂾂��ǂ�..(^^; */
	return (255 * val + max/2) / max;
}


/*
**		�T���v���l��ǂ� (Raw, maxval>255)
*/
static DWORD FASTCALL
 GetRawValG(PNM *lpPnm)
{
	SPI_FILE *fp = lpPnm->fp;
	DWORD max = lpPnm->maxval;
	DWORD msb, lsb;

	/*
	 * Netpbm Release 9.9 (2000/11/20��) �ł̎����ɏ]���A
	 * ���̏ꍇ�̃T���v���l�� big-endian (MSB-first) �`��
	 * 2�o�C�g�����ŋL�^����Ă�����̂Ƃ��Ĉ����܂��B
	 *
	 * �� Plug-in �̈ȑO�̃o�[�W����(0.11�ȑO)�ł́A
	 * little-endian (LSB-first) �`���Ƃ��ĉ��߂��Ă��܂����B
	 */
	msb = SpiGetByte(fp);
	lsb = SpiGetByte(fp);

	return (255 * (lsb | msb << 8) + max/2) / max;
}

