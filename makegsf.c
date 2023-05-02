#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <locale.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include <iconv.h>
#include <zlib.h>


/************* Dynamically allocated data buffer ****************/

#define DEFAULT_BUFFER_T {NULL,0,0}

typedef struct {
	void * data;
	size_t size;
	size_t max;
} buffer_t;

int is_buffer_new(buffer_t * buf)
{
	return buf->data == NULL;
}

void init_buffer(buffer_t * buf, size_t initial_max)
{
	buf->data = malloc(initial_max);
	buf->size = 0;
	buf->max = initial_max;
}

void init_new_buffer(buffer_t * buf, size_t initial_max)
{
	if (is_buffer_new(buf))
		init_buffer(buf, initial_max);
}

void expand_buffer(buffer_t * buf, size_t new_max)
{
	size_t old_max = buf->max;
	while (buf->max < new_max)
	{
		buf->max *= 2;
	}
	if (buf->max > old_max)
	{
		buf->data = realloc(buf->data,buf->max);
	}
}

void set_buffer(buffer_t * buf, const void * data, size_t size)
{
	init_new_buffer(buf, size);
	expand_buffer(buf, size);
	memcpy(buf->data, data, size);
	buf->size = size;
}

void append_buffer(buffer_t * buf, const void * data, size_t size)
{
	expand_buffer(buf, buf->size+size);
	memcpy(buf->data+buf->size, data, size);
	buf->size += size;
}

void append_buffer_char(buffer_t * buf, const char ch)
{
	expand_buffer(buf, buf->size+sizeof(ch));
	*((char*)(buf->data+buf->size)) = ch;
	buf->size += sizeof(ch);
}

void append_buffer_wchar(buffer_t * buf, const wchar_t ch)
{
	expand_buffer(buf, buf->size+sizeof(ch));
	*((wchar_t*)(buf->data+buf->size)) = ch;
	buf->size += sizeof(ch);
}

void copy_buffer(buffer_t * dest_buf, buffer_t * src_buf)
{
	if (!src_buf || !dest_buf || is_buffer_new(src_buf))
		return;
	
	init_new_buffer(dest_buf, src_buf->size);
	expand_buffer(dest_buf, src_buf->size);
	
	memcpy(dest_buf->data, src_buf->data, src_buf->size);
	dest_buf->size = src_buf->size;
}

void free_buffer(buffer_t * buf)
{
	free(buf->data);
	buf->data = NULL;
	buf->size = 0;
	buf->max = 0;
}






/************************ Types ********************************/

enum {
	TOK_ID = 0,
	TOK_NUM,
	TOK_STR
};

typedef struct {
	int type;
	/*
		ID: same as text
		NUM: unsigned integer value
		STR: text with escape sequences interpreted and no quotes
	*/
	void * value;
} token_t;

typedef struct {
	buffer_t name_buf;
	buffer_t value_buf;
} gsf_tag_t;





/******************** Global variables **************************/

FILE * script_file = NULL;
wchar_t * script_name = NULL;
unsigned script_line = 0;

unsigned entry_point = 0x8000000;
buffer_t filename_template_buf = DEFAULT_BUFFER_T;
unsigned minigsf_offset = 0;
unsigned song_number = 1;
unsigned song_id;
buffer_t gsf_tag_buf = DEFAULT_BUFFER_T;






/******************** Utility ******************************/

int wcscasecmp(const wchar_t * ws1, const wchar_t * ws2)
{
	size_t i = 0;
	while (1)
	{
		wchar_t ch1 = towlower(ws1[i]);
		wchar_t ch2 = towlower(ws2[i]);
		if (ch1 != ch2) return ch1 - ch2;
		if (ch1 == L'\0') return 0;
		i++;
	}
}




/******************* Multi-byte I/O ******************************/

int fput32(unsigned v, FILE *f)
{
	for (int b = 0; b < 4; b++)
	{
		if (fputc(v,f) == EOF)
			return EOF;
		v >>= 8;
	}
	return ~EOF;
}

void write32(uint8_t *p, unsigned v)
{
	p[0] = v;
	p[1] = v>>8;
	p[2] = v>>16;
	p[3] = v>>24;
}






/********************* Error reporting *****************************/

void msg_prologue()
{
	if (script_name)
		wprintf(L"%ls:",script_name);
	if (script_line)
		printf("%u:",script_line);
	
	if (script_name || script_line)
		putchar(' ');
}

void warn(char * msg, ...)
{
	msg_prologue();
	
	va_list args;
	va_start(args,msg);
	vprintf(msg,args);
	va_end(args);
	putchar('\n');
}

void err(char * msg, ...)
{
	msg_prologue();
	
	va_list args;
	va_start(args,msg);
	vprintf(msg,args);
	va_end(args);
	putchar('\n');
}

void wwarn(wchar_t * msg, ...)
{
	msg_prologue();
	
	va_list args;
	va_start(args,msg);
	vwprintf(msg,args);
	va_end(args);
	putchar('\n');
}

void werr(wchar_t * msg, ...)
{
	msg_prologue();
	
	va_list args;
	va_start(args,msg);
	vwprintf(msg,args);
	va_end(args);
	putchar('\n');
}






/********************* Iconv interface ****************************/

size_t iconv_2(const char * to, const char * from, buffer_t * dest_buf, void * src, size_t src_size)
{
	if (!dest_buf)
		return 0;
	init_new_buffer(dest_buf,0x200);
	
	iconv_t ic = iconv_open(to,from);
	if (ic == (iconv_t)-1)
	{
		err("Unsupported conversion");
		return 0;
	}
	
	void * src_ptr = src;
	size_t src_left = src_size;
	void * dest_ptr = dest_buf->data + dest_buf->size;
	size_t dest_left = dest_buf->max - dest_buf->size;
	size_t dest_initial_left = dest_left;
	size_t dest_initial_size = dest_buf->size;
	while (1)
	{
		size_t status = iconv(ic,(char**)&src_ptr,&src_left,(char**)&dest_ptr,&dest_left);
		if (status == (size_t)-1)
		{
			int en = errno;
			if (en == E2BIG)
			{
				dest_buf->size += dest_initial_left - dest_left;
				size_t old = dest_buf->max;
				expand_buffer(dest_buf, dest_buf->max*2);
				dest_left += dest_buf->max - old;
				dest_initial_left = dest_left;
			}
			else
			{
				switch (en)
				{
					case EILSEQ:
						err("Invalid character at index %llu",src_ptr-src);
						break;
					case EINVAL:
						err("Incomplete character at index %llu",src_ptr-src);
						break;
					default:
						err("Conversion failure (%d)",errno);
						break;
				}
				iconv_close(ic);
				return 0;
			}
		}
		else
		{
			break;
		}
	}
	dest_buf->size += dest_initial_left - dest_left;
	iconv_close(ic);
	
	return dest_buf->size - dest_initial_size;
}







/********************** Script I/O ******************************/

FILE * open_script(char * src_filename)
{
	/* open */
	script_file = fopen(src_filename,"r");
	script_name = NULL;
	script_line = 0;
	if (!script_file)
	{
		printf("Can't open %s: %s\n", src_filename, strerror(errno));
		script_file = NULL;
		return NULL;
	}
	
	/* find index of the final path separator */
	size_t base_name_index = 0;
	size_t src_filename_size = 0;
	while (1)
	{
		char ch = src_filename[src_filename_size];
		if (ch == '\0')
			break;
		if (ch == '/' || ch == '\\')
			base_name_index = src_filename_size+1;
		
		src_filename_size++;
	}
	
	/* convert base filename to wchars for future printing */
	static buffer_t filename_buf = DEFAULT_BUFFER_T;
	filename_buf.size = 0;
	if (iconv_2("wchar_t","", &filename_buf, src_filename+base_name_index,src_filename_size-base_name_index+1))
		script_name = filename_buf.data;
	
	/* if needed, chdir to location of script file */
	if (base_name_index)
	{
		src_filename[base_name_index] = '\0';
		if (chdir(src_filename))
		{
			printf("WARNING: Can't change directory to %s (%s), may fail\n",src_filename,strerror(errno));
		}
	}
	
	return script_file;
}

void close_script()
{
	fclose(script_file);
	script_name = NULL;
}


int script_ferror()
{
	return ferror(script_file);
}

int script_feof()
{
	return feof(script_file);
}

wchar_t * read_script_line()
{
	++script_line;
	if (script_feof()) return NULL;
	
	/* read raw UTF-8 */
	static buffer_t src_buf = DEFAULT_BUFFER_T;
	init_new_buffer(&src_buf,0x200);
	src_buf.size = 0;
	while (1)
	{
		int ch = fgetc(script_file);
		if (ch == EOF)
		{
			if (script_ferror())
			{
				err(strerror(errno));
				return NULL;
			}
			if (script_feof())
				break;
		}
		if (ch == '\n')
			break;
		append_buffer_char(&src_buf, ch);
	}
	if (((char*)src_buf.data)[src_buf.size-1] == '\r')  /* remove CR from CRLF */
		--(src_buf.size);
	append_buffer_char(&src_buf, '\0');
	
	/* convert line to wchars */
	static buffer_t out_buf = DEFAULT_BUFFER_T;
	init_new_buffer(&out_buf,0x200);
	out_buf.size = 0;
	if (!iconv_2("wchar_t","UTF-8", &out_buf, src_buf.data,src_buf.size))
		return NULL;
	return out_buf.data;
}







/*********************** Script parsing ****************************/

const char * get_token_type_name(int type)
{
	switch (type)
	{
		case TOK_ID:
			return "identifier";
		case TOK_NUM:
			return "number";
		case TOK_STR:
			return "string";
		default:
			return "invalid";
	}
}


token_t * parse_one_token(wchar_t * line_start)
{
	static wchar_t * line = NULL;
	static size_t index = 0;
	static token_t token;
	
	/* if a pointer is supplied, reset parsing from scratch */
	if (line_start)
	{
		line = line_start;
		index = 0;
	}
	
	/* don't parse if nothing loaded */
	if (line == NULL)
		return NULL;
	
	/* skip leading whitespace */
	wchar_t ch = line[index];
	while (1)
	{
		if (iswspace(ch))
		{
			ch = line[++index];
		}
		else if (ch == L'\0' || ch == L'#')
		{ /* break on EOL or comment */
			line = NULL;
			return NULL;
		}
		else
		{
			break;
		}
	}
	
	/* non-whitespace char found, try parsing */
	static buffer_t string_buf = DEFAULT_BUFFER_T;
	
	if (ch == L'\"')
	{ /* try parsing string */
		token.type = TOK_STR;
		
		init_new_buffer(&string_buf, 0x200);
		string_buf.size = 0;
		
		/* keep parsing until the end quote */
		int end_flag = 0;
		while (!end_flag)
		{
			ch = line[++index];
			if (ch == L'\0')
			{
				err("String with no end quote");
				line = NULL;
				return NULL;
			}
			else if (ch == L'\"')
			{ /* end */
				ch = L'\0';
				end_flag = 1;
			}
			else if (ch == L'\\')
			{ /* escape */
				ch = line[++index];
				if (ch == L'\0')
				{
					/* we don't have the next line so escaping newlines won't work */
					err("Escaping newlines is not supported");
					line = NULL;
					return NULL;
				}
				else if (ch == L'n')
				{
					ch = L'\n';
				}
			}
			
			append_buffer_wchar(&string_buf, ch);
		}
		token.value = string_buf.data;
		index++; /* because we're still pointing at the end quote */
	}
	else if (ch == L'$' || iswdigit(ch))
	{ /* try parsing number */
		token.type = TOK_NUM;
		
		int hex = 0;
		intptr_t out = 0;
		if (ch == L'$')
		{
			hex = 1;
			ch = line[index += 1];
		}
		else if (ch == L'0' && line[index+1] == L'x')
		{
			hex = 1;
			ch = line[index += 2];
		}
		
		int error = 0;
		while (1)
		{
			unsigned digit = 0;
			if (iswspace(ch) || ch == L'\0' || ch == L'#')
			{ /* stop parsing on EOL, whitespace, or comment */
				break;
			}
			else if (ch >= L'0' && ch <= L'9')
				digit = ch - L'0';
			else if (hex && ch >= L'A' && ch <= L'F')
				digit = ch - L'A' + 0x0a;
			else if (hex && ch >= L'a' && ch <= L'f')
				digit = ch - L'a' + 0x0a;
			else
			{
				werr(L"Can't parse %lc as digit",ch);
				error++;
			}
			
			if (hex)
			{
				out <<= 4;
				out |= digit;
			}
			else
			{
				out *= 10;
				out += digit;
			}
			ch = line[++index];
		}
		
		if (error)
		{
			line = NULL;
			return NULL;
		}
		
		token.value = (void*)out;
	}
	else
	{ /* try parsing identifier */
		token.type = TOK_ID;
		
		init_new_buffer(&string_buf, 0x200);
		string_buf.size = 0;
		
		/* keep parsing until whitespace, comment, or EOL */
		int end_flag = 0;
		while (!end_flag)
		{
			if (iswspace(ch) || ch == L'\0' || ch == L'#')
			{
				ch = L'\0';
				end_flag = 1;
			}
			
			append_buffer_wchar(&string_buf, ch);
			
			if (!end_flag)
				ch = line[++index];
		}
		
		token.value = string_buf.data;
	}
	
	
	return &token;
}


token_t * parse_one_token_type(wchar_t * line_start, int expected_type)
{
	token_t * tok = parse_one_token(line_start);
	if (!tok)
		return NULL;
	
	if (tok->type != expected_type)
	{
		err("Expected %s, got %s", get_token_type_name(expected_type),get_token_type_name(tok->type));
		return NULL;
	}
	
	return tok;
}







/****************************** Tags ****************************/

gsf_tag_t * get_gsf_tag(wchar_t * name)
{
	for (size_t i = 0; i < gsf_tag_buf.size; i += sizeof(gsf_tag_t))
	{
		gsf_tag_t * cmp_tag = gsf_tag_buf.data + i;
		if (!is_buffer_new(&cmp_tag->name_buf))
		{
			if (!wcscmp(name,cmp_tag->name_buf.data))
			{
				return cmp_tag;
			}
		}
	}
	return NULL;
}

wchar_t * get_gsf_tag_value(wchar_t *name)
{
	gsf_tag_t * found_tag = get_gsf_tag(name);
	if (found_tag && !is_buffer_new(&found_tag->value_buf))
	{
		return found_tag->value_buf.data;
	}
	return NULL;
}

void set_gsf_tag(wchar_t * name, wchar_t * value)
{
	/* check if this tag already exists in the list */
	init_new_buffer(&gsf_tag_buf, 0x10*sizeof(gsf_tag_t));
	gsf_tag_t new_tag = {DEFAULT_BUFFER_T,DEFAULT_BUFFER_T};  /* if needed... */
	gsf_tag_t * found_tag = get_gsf_tag(name);
	
	/* if the tag is not new, replace its value buffer */
	size_t value_len = value ? wcslen(value) : 0;
	size_t value_size = (value_len+1)*sizeof(wchar_t);
	if (found_tag)
	{
		buffer_t * value_buf = &found_tag->value_buf;
		if (value_len)
		{
			set_buffer(value_buf, value, value_size);
		}
		else
		{
			free_buffer(value_buf);
		}
	}
	/* if the tag IS new, create a new tag buffer entry */
	else
	{
		if (value_len)
		{
			size_t name_len = wcslen(name);
			size_t name_size = (name_len+1)*sizeof(wchar_t);
			init_buffer(&new_tag.name_buf, name_size);
			init_buffer(&new_tag.value_buf, value_size);
			set_buffer(&new_tag.name_buf, name, name_size);
			set_buffer(&new_tag.value_buf, value, value_size);
			append_buffer(&gsf_tag_buf, &new_tag, sizeof(new_tag));
		}
	}
}

int gsf_tag_name_ok(wchar_t * name)
{
	int error = 0;
	size_t name_len = wcslen(name);
	if (!name_len)
	{
		err("GSF tag name is blank");
		error++;
	}
	int bad_tag_name = 0;
	if (name[0] == L'_' || !wcscmp(name,L"filedir") || !wcscmp(name,L"filename") || !wcscmp(name,L"fileext"))
	{
		werr(L"GSF tag name %ls is reserved", name);
		error++;
	}
	for (size_t i = 0; i < name_len; i++)
	{
		wchar_t ch = name[i];
		if (!iswalnum(ch) && ch != L'_')
			bad_tag_name++;
		if (!iswlower(ch))
			name[i] = towlower(ch);
	}
	if (bad_tag_name)
	{
		werr(L"Invalid GSF tag name %ls", name);
		return 0;
	}
	if (error)
		return 0;
	
	return 1;
}

token_t * parse_set_gsf_tag(wchar_t * name)
{
	token_t * value_tok = parse_one_token_type(NULL,TOK_STR);
	if (value_tok)
	{
		wchar_t * value = value_tok->value;
		set_gsf_tag(name,value);
	}
	else
	{
		set_gsf_tag(name,NULL);
	}
	return value_tok;
}

token_t * parse_set_gsf_tag_optional(wchar_t * name)
{
	token_t * value_tok = parse_one_token_type(NULL,TOK_STR);
	if (value_tok)
	{
		wchar_t * value = value_tok->value;
		set_gsf_tag(name,value);
	}
	return value_tok;
}




/********************** generic gsf-related ************************/

char * get_os_filename(wchar_t * filename)
{
	/** remove any non-filename-valid characters **/
	size_t filename_len = wcslen(filename);
	size_t index = 0;
	while (1)
	{
		wchar_t ch = filename[index];
		if (ch == '\0')
			break;
		else if (ch < 0x20 || wcschr(L"<>:\"/\\|?*", ch))
		{
			memmove(filename+index, filename+index+1, (filename_len-index)*sizeof(wchar_t));
			filename_len--;
		}
		else
		{
			index++;
		}
	}
	
	/** convert to os-preferred format **/
	static buffer_t os_filename_buf = DEFAULT_BUFFER_T;
	os_filename_buf.size = 0;
	iconv_2("","wchar_t", &os_filename_buf, filename, (filename_len+1)*sizeof(wchar_t));
	return os_filename_buf.data;
}

void write_gsf_data_to_file(FILE * f, uint8_t * data, size_t size)
{
	/* compress the program data */
	z_stream zs;
	memset(&zs,0,sizeof(zs));
	int status;
	if ((status = deflateInit(&zs, Z_DEFAULT_COMPRESSION)) != Z_OK)
	{
		err("Error %d initializing zlib",status);
		return;
	}
	
	static buffer_t out_buf = DEFAULT_BUFFER_T;
	init_new_buffer(&out_buf,0x10000);
	out_buf.size = 0;
	
	zs.next_in = data;
	zs.avail_in = size;
	zs.next_out = out_buf.data;
	zs.avail_out = out_buf.max;
	while (1)
	{
		status = deflate(&zs, zs.avail_in ? Z_NO_FLUSH : Z_FINISH);
		out_buf.size = zs.total_out;
		if (status == Z_STREAM_END)
		{
			break;
		}
		else if (status != Z_OK)
		{
			err("Error %d during zlib compression",status);
			deflateEnd(&zs);
			free_buffer(&out_buf);
			return;
		}
		else if (zs.avail_out == 0)
		{
			expand_buffer(&out_buf, out_buf.max*2);
			zs.next_out = out_buf.data + out_buf.size;
			zs.avail_out = out_buf.max - out_buf.size;
		}
	}
	
	deflateEnd(&zs);
	
	/* calculate compressed crc */
	uLong crc = crc32(0L, Z_NULL, 0);
	crc = crc32(crc,out_buf.data,out_buf.size);
	
	/* write the program data */
	fputc('P',f);
	fputc('S',f);
	fputc('F',f);
	fputc(0x22,f);
	fput32(0,f);
	fput32(out_buf.size,f);
	fput32(crc,f);
	fwrite(out_buf.data,1,out_buf.size,f);
	
	free_buffer(&out_buf);
}

void write_gsf_tags_to_file(FILE *f)
{
	fwrite("[TAG]",1,5,f);
	for (size_t i = 0; i < gsf_tag_buf.size; i += sizeof(gsf_tag_t))
	{
		gsf_tag_t * cmp_tag = gsf_tag_buf.data + i;
		if (!is_buffer_new(&cmp_tag->name_buf) && !is_buffer_new(&cmp_tag->value_buf))
		{
			wchar_t * name = cmp_tag->name_buf.data;
			size_t name_size = cmp_tag->name_buf.size - sizeof(wchar_t);
			wchar_t * value = cmp_tag->value_buf.data;
			size_t value_size = cmp_tag->value_buf.size - sizeof(wchar_t);
			if (name && value)
			{
				static buffer_t out_name_buf = DEFAULT_BUFFER_T;
				static buffer_t out_value_buf = DEFAULT_BUFFER_T;
				out_name_buf.size = 0;
				out_value_buf.size = 0;
				iconv_2("UTF-8","wchar_t", &out_name_buf, name,name_size);
				iconv_2("UTF-8","wchar_t", &out_value_buf, value,value_size);
				
				fwrite(out_name_buf.data,1,out_name_buf.size,f);
				fputc('=',f);
				for (size_t i = 0; i < out_value_buf.size; i++)
				{
					char ch = ((char*)out_value_buf.data)[i];
					if (ch == '\n')
					{ /* separate lines of a value must have the name= on each line */
						fputc('\n',f);
						fwrite(out_name_buf.data,1,out_name_buf.size,f);
						fputc('=',f);
					}
					else
					{
						fputc(ch,f);
					}
				}
				fputc('\n',f);
			}
		}
	}
	fwrite("utf8=1",1,6,f);
}







/************************ gsflib-related ***************************/

void make_gsflib(wchar_t * inname, wchar_t * outname)
{
	if (get_gsf_tag(L"_lib"))
	{
		err("gsflib filename already defined");
		return;
	}
	
	char * os_filename = get_os_filename(inname);
	FILE *f = fopen(os_filename,"rb");
	if (!f)
	{
		werr(L"Can't open %ls for reading (%s). Output .minigsfs may not work.",inname,strerror(errno));
		return;
	}
	static buffer_t in_buf = DEFAULT_BUFFER_T;
	init_new_buffer(&in_buf,0x10000);
	write32(in_buf.data+0, entry_point);
	write32(in_buf.data+4, entry_point);
	in_buf.size = 0xc;
	while (1)
	{
		int ch = fgetc(f);
		if (ch == EOF) break;
		append_buffer_char(&in_buf,ch);
	}
	write32(in_buf.data+8, in_buf.size-0xc);
	if (ferror(f))
	{
		werr(L"Error while reading %ls (%s). Output .minigsfs may not work.",inname,strerror(errno));
	}
	fclose(f);
	
	os_filename = get_os_filename(outname);
	f = fopen(os_filename,"wb");
	if (!f)
	{
		werr(L"Can't open %ls for writing (%s). Output .minigsfs may not work.",outname,strerror(errno));
		free_buffer(&in_buf);
		return;
	}
	
	write_gsf_data_to_file(f, in_buf.data, in_buf.size);
	
	fclose(f);
	free_buffer(&in_buf);
}





/************************ minigsf-related **************************/

void make_minigsf()
{
	if (!get_gsf_tag(L"_lib"))
	{
		err("gsflib filename not defined yet");
		return;
	}
	if (is_buffer_new(&filename_template_buf))
	{
		err("Filename template not defined yet");
		return;
	}
	
	
	/** transform filename template to real filename **/
	wchar_t * filename_template = filename_template_buf.data;
	static buffer_t filename_buf = DEFAULT_BUFFER_T;
	init_new_buffer(&filename_buf, 0x200);
	filename_buf.size = 0;
	size_t index = 0;
	while (1)
	{
		wchar_t ch = filename_template[index++];
		if (ch == L'\0')
			break;
		else if (ch == L'%')
		{ /* conversion code */
			unsigned number = 0;
			while (1)
			{
				ch = filename_template[index++];
				if (ch == L'\0')
				{
					err("Incomplete conversion specifier in filename template");
					return;
				}
				else if (iswdigit(ch))
				{
					unsigned digit = ch - L'0';
					number *= 10;
					number += digit;
				}
				else if (ch == L'n')
				{ /* song number */
					wchar_t out_buf[0x10];
					size_t written = swprintf(out_buf,0x10, L"%0*u", number,song_number);
					append_buffer(&filename_buf,out_buf,written*sizeof(wchar_t));
					break;
				}
				else if (ch == L'i')
				{ /* song id */
					wchar_t out_buf[0x10];
					size_t written = swprintf(out_buf,0x10, L"%0*u", number,song_id);
					append_buffer(&filename_buf,out_buf,written*sizeof(wchar_t));
					break;
				}
				else if (ch == L't')
				{ /* title */
					gsf_tag_t * tag = get_gsf_tag(L"title");
					if (tag && !is_buffer_new(&tag->value_buf))
						append_buffer(&filename_buf,tag->value_buf.data,tag->value_buf.size-sizeof(wchar_t));
					else
						warn("Title conversion specifier requested, but is not defined");
					break;
				}
				else if (ch == L'a')
				{ /* artist */
					gsf_tag_t * tag = get_gsf_tag(L"artist");
					if (tag && !is_buffer_new(&tag->value_buf))
						append_buffer(&filename_buf,tag->value_buf.data,tag->value_buf.size-sizeof(wchar_t));
					else
						warn("Artist conversion specifier requested, but is not defined");
					break;
				}
				else
				{
					err("Invalid conversion specifier '%lc' in filename template",ch);
					return;
				}
			}
		}
		else
		{
			append_buffer_wchar(&filename_buf,ch);
		}
	}
	append_buffer_wchar(&filename_buf,'\0');
	
	
	
	/** save minigsf data **/
	char * os_filename = get_os_filename(filename_buf.data);
	FILE *f = fopen(os_filename,"wb");
	if (!f)
	{
		wprintf(L"Can't open %ls for writing (%s)", filename_buf.data, strerror(errno));
		return;
	}
	uint8_t program_data[0x10];
	write32(program_data+0, entry_point);
	write32(program_data+4, minigsf_offset);
	write32(program_data+8, 4);
	write32(program_data+0xc, song_id);
	
	write_gsf_data_to_file(f, program_data, 0x10);
	write_gsf_tags_to_file(f);
	
	fclose(f);
	
	song_number++;
}








/*************************** Main *****************************/

int main(int argc, char *argv[])
{
	setlocale(LC_ALL,"");
	
	if (argc != 2)
	{
		puts("usage: makegsf scriptfile");
		return EXIT_FAILURE;
	}
	
	
	
	open_script(argv[1]);
	
	while (1)
	{
		wchar_t * line = read_script_line();
		if (!line)
			break;
		
		token_t * cmd_tok = parse_one_token_type(line,TOK_ID);
		if (cmd_tok)
		{
			wchar_t * n = cmd_tok->value;
			/************ gsflib-related ***************/
			if (!wcscasecmp(n,L"MultiBoot"))
				entry_point = 0x2000000;
			else if (!wcscasecmp(n,L"MakeGSFLib"))
			{
				token_t * tok = parse_one_token_type(NULL,TOK_STR);
				if (tok)
				{
					wchar_t * inname = wcsdup(tok->value);
					tok = parse_one_token_type(NULL,TOK_STR);
					if (tok)
					{
						wchar_t * outname = tok->value;
						make_gsflib(inname,outname);
						set_gsf_tag(L"_lib", outname);
					}
					else
					{
						err("Can't get gsflib filename value");
					}
					free(inname);
				}
				else
				{
					err("Can't get source filename value");
				}
			}
			else if (!wcscasecmp(n,L"GSFLib"))
			{
				if (!get_gsf_tag(L"_lib"))
				{
					token_t * name_tok = parse_one_token_type(NULL,TOK_STR);
					if (name_tok)
					{
						wchar_t * value = name_tok->value;
						set_gsf_tag(L"_lib", value);
					}
					else
					{
						err("Can't get gsflib filename value");
					}
				}
				else
				{
					err("gsflib filename already defined");
				}
			}
			/************* tag-related *****************/
			else if (!wcscasecmp(n,L"Title"))
				parse_set_gsf_tag(L"title");
			else if (!wcscasecmp(n,L"Artist"))
				parse_set_gsf_tag(L"artist");
			else if (!wcscasecmp(n,L"Game"))
				parse_set_gsf_tag(L"game");
			else if (!wcscasecmp(n,L"Date"))
				parse_set_gsf_tag(L"year");
			else if (!wcscasecmp(n,L"Year"))
				parse_set_gsf_tag(L"year");
			else if (!wcscasecmp(n,L"Genre"))
				parse_set_gsf_tag(L"genre");
			else if (!wcscasecmp(n,L"Comment"))
				parse_set_gsf_tag(L"comment");
			else if (!wcscasecmp(n,L"Copyright"))
				parse_set_gsf_tag(L"copyright");
			else if (!wcscasecmp(n,L"GSFBy"))
				parse_set_gsf_tag(L"gsfby");
			else if (!wcscasecmp(n,L"Volume"))
				parse_set_gsf_tag(L"volume");
			else if (!wcscasecmp(n,L"Length"))
				parse_set_gsf_tag(L"length");
			else if (!wcscasecmp(n,L"Fade"))
				parse_set_gsf_tag(L"fade");
			else if (!wcscasecmp(n,L"Tag"))
			{
				token_t * name_tok = parse_one_token_type(NULL,TOK_STR);
				if (name_tok)
				{
					wchar_t * name = wcsdup(name_tok->value);
					if (gsf_tag_name_ok(name))
						parse_set_gsf_tag(name);
					free(name);
				}
			}
			/*************** minigsf-related **************/
			else if (!wcscasecmp(n,L"FilenameTemplate"))
			{
				token_t * template_tok = parse_one_token_type(NULL,TOK_STR);
				if (template_tok)
				{
					wchar_t * value = template_tok->value;
					set_buffer(&filename_template_buf,value,(wcslen(value)+1)*sizeof(wchar_t));
				}
				else
				{
					err("Can't get filename template value");
				}
			}
			else if (!wcscasecmp(n,L"MiniGSFOffset"))
			{
				token_t * offset_tok = parse_one_token_type(NULL,TOK_NUM);
				if (offset_tok)
				{
					minigsf_offset = (intptr_t)offset_tok->value;
				}
				else
				{
					err("Can't get minigsf offset value");
				}
			}
			else if (!wcscasecmp(n,L"SetSongNumber"))
			{
				token_t * tok = parse_one_token_type(NULL,TOK_NUM);
				if (tok)
				{
					song_number = (intptr_t)tok->value;
				}
				else
				{
					err("Can't get song number value");
				}
			}
			else if (!wcscasecmp(n,L"MakeMiniGSF"))
			{
				token_t * id_tok = parse_one_token_type(NULL,TOK_NUM);
				if (id_tok)
				{
					song_id = (intptr_t)id_tok->value;
					if (parse_set_gsf_tag_optional(L"title"))
						if (parse_set_gsf_tag_optional(L"artist"))
							if (parse_set_gsf_tag_optional(L"comment"))
								if (parse_set_gsf_tag_optional(L"length"))
									if (parse_set_gsf_tag_optional(L"fade"))
										if (parse_set_gsf_tag_optional(L"volume"))
											if (parse_set_gsf_tag_optional(L"genre"))
											{ }
					make_minigsf();
				}
				else
				{
					err("Can't get song ID value");
				}
			}
			else if (!wcscasecmp(n,L"MakeMiniGSFRange"))
			{
				unsigned start;
				unsigned end;
				unsigned step = 1;
				
				token_t * tok = parse_one_token_type(NULL,TOK_NUM);
				if (tok)
				{
					start = (intptr_t)tok->value;
					tok = parse_one_token_type(NULL,TOK_NUM);
					if (tok)
					{
						end = (intptr_t)tok->value;
						tok = parse_one_token_type(NULL,TOK_NUM);
						if (tok)
						{
							step = (intptr_t)tok->value;
						}
						if (step <= 0)
						{
							err("Invalid step value");
						}
						else
						{
							for (song_id = start; song_id <= end; song_id += step)
								make_minigsf();
						}
					}
					else
					{
						err("Can't get range end value");
					}
				}
				else
				{
					err("Can't get range start value");
				}
			}
			/*************** invalid ****************/
			else
				werr(L"Unrecognized command %ls",n);
		}
	}
	
	close_script();
	
	return EXIT_SUCCESS;
}