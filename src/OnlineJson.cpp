#include "global.h"
#include "OnlineJson.h"
#include "RageUtil.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>

static const JsonValue g_Null;

JsonValue JsonValue::Bool( bool b )
{
	JsonValue v;
	v.m_Type = TYPE_BOOL;
	v.m_bBool = b;
	return v;
}

JsonValue JsonValue::Int( int i )
{
	JsonValue v;
	v.m_Type = TYPE_NUMBER;
	v.m_fNumber = i;
	v.m_sText = ssprintf( "%i", i );
	return v;
}

JsonValue JsonValue::Float( float f )
{
	JsonValue v;
	v.m_sText = JsonFloatText( f );
	if( v.m_sText == "null" )
		return v;
	v.m_Type = TYPE_NUMBER;
	v.m_fNumber = f;
	return v;
}

JsonValue JsonValue::Double( double f )
{
	JsonValue v;
	if( f != f || f > DBL_MAX || f < -DBL_MAX )
		return v;
	v.m_Type = TYPE_NUMBER;
	v.m_fNumber = f;
	v.m_sText = ssprintf( "%.17g", f );
	return v;
}

JsonValue JsonValue::Number( double f, const CString &sText )
{
	JsonValue v;
	v.m_Type = TYPE_NUMBER;
	v.m_fNumber = f;
	v.m_sText = sText;
	return v;
}

JsonValue JsonValue::String( const CString &s )
{
	JsonValue v;
	v.m_Type = TYPE_STRING;
	v.m_sText = s;
	return v;
}

JsonValue JsonValue::Array()
{
	JsonValue v;
	v.m_Type = TYPE_ARRAY;
	return v;
}

JsonValue JsonValue::Object()
{
	JsonValue v;
	v.m_Type = TYPE_OBJECT;
	return v;
}

JsonValue JsonValue::Raw( const CString &sJson )
{
	JsonValue v;
	v.m_Type = TYPE_RAW;
	v.m_sText = sJson;
	return v;
}

bool JsonValue::GetBool( bool bDefault ) const
{
	return m_Type == TYPE_BOOL? m_bBool: bDefault;
}

double JsonValue::GetNumber( double fDefault ) const
{
	return m_Type == TYPE_NUMBER? m_fNumber: fDefault;
}

int JsonValue::GetInt( int iDefault ) const
{
	if( m_Type != TYPE_NUMBER || m_fNumber > 2147483647.0 || m_fNumber < -2147483648.0 )
		return iDefault;
	return (int) m_fNumber;
}

CString JsonValue::GetString( const CString &sDefault ) const
{
	return m_Type == TYPE_STRING? m_sText: sDefault;
}

const JsonValue &JsonValue::operator[]( unsigned i ) const
{
	if( (m_Type != TYPE_ARRAY && m_Type != TYPE_OBJECT) || i >= m_Children.size() )
		return g_Null;
	return m_Children[i];
}

const JsonValue &JsonValue::operator[]( const char *szKey ) const
{
	if( m_Type != TYPE_OBJECT )
		return g_Null;
	for( unsigned i = 0; i < m_Keys.size(); ++i )
		if( m_Keys[i] == szKey )
			return m_Children[i];
	return g_Null;
}

bool JsonValue::Has( const char *szKey ) const
{
	if( m_Type != TYPE_OBJECT )
		return false;
	for( unsigned i = 0; i < m_Keys.size(); ++i )
		if( m_Keys[i] == szKey )
			return true;
	return false;
}

JsonValue &JsonValue::Set( const CString &sKey, const JsonValue &v )
{
	ASSERT( m_Type == TYPE_OBJECT );
	for( unsigned i = 0; i < m_Keys.size(); ++i )
		if( m_Keys[i] == sKey )
		{
			m_Children[i] = v;
			return m_Children[i];
		}
	m_Keys.push_back( sKey );
	m_Children.push_back( v );
	return m_Children.back();
}

JsonValue &JsonValue::Append( const JsonValue &v )
{
	ASSERT( m_Type == TYPE_ARRAY );
	m_Children.push_back( v );
	return m_Children.back();
}

CString JsonQuote( const CString &s )
{
	const bool bUTF8 = utf8_is_valid( s );
	CString out;
	out.reserve( s.size() + 2 );
	out += '"';
	for( unsigned i = 0; i < s.size(); ++i )
	{
		const unsigned char c = s[i];
		switch( c )
		{
		case '"':	out += "\\\"";	break;
		case '\\':	out += "\\\\";	break;
		case '\n':	out += "\\n";	break;
		case '\r':	out += "\\r";	break;
		case '\t':	out += "\\t";	break;
		default:
			if( c < 0x20 || c == 0x7f || (c >= 0x80 && !bUTF8) )
				out += ssprintf( "\\u%04x", c );
			else
				out += (char) c;
		}
	}
	out += '"';
	return out;
}

CString JsonFloatText( float f )
{
	if( f != f || f > FLT_MAX || f < -FLT_MAX )
		return "null";
	return ssprintf( "%.9g", f );
}

void JsonValue::Write( CString &sOut ) const
{
	switch( m_Type )
	{
	case TYPE_NULL:		sOut += "null";	break;
	case TYPE_BOOL:		sOut += m_bBool? "true": "false";	break;
	case TYPE_NUMBER:	sOut += m_sText;	break;
	case TYPE_RAW:		sOut += m_sText;	break;
	case TYPE_STRING:	sOut += JsonQuote( m_sText );	break;
	case TYPE_ARRAY:
		sOut += '[';
		for( unsigned i = 0; i < m_Children.size(); ++i )
		{
			if( i )
				sOut += ',';
			m_Children[i].Write( sOut );
		}
		sOut += ']';
		break;
	case TYPE_OBJECT:
		sOut += '{';
		for( unsigned i = 0; i < m_Children.size(); ++i )
		{
			if( i )
				sOut += ',';
			sOut += JsonQuote( m_Keys[i] );
			sOut += ':';
			m_Children[i].Write( sOut );
		}
		sOut += '}';
		break;
	}
}

CString JsonValue::Write() const
{
	CString s;
	Write( s );
	return s;
}

/* A recursive descent reader.  Nesting is limited so a hostile server can't
 * run us out of stack. */
namespace
{
	const int MAX_DEPTH = 64;

	struct Reader
	{
		const char *p, *end;
		CString sError;

		Reader( const CString &s ): p(s.data()), end(s.data() + s.size()) { }

		bool Fail( const char *szWhy )
		{
			if( sError.empty() )
				sError = szWhy;
			return false;
		}

		void SkipSpace()
		{
			while( p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') )
				++p;
		}

		bool Literal( const char *szWord )
		{
			const char *q = p;
			for( ; *szWord; ++szWord, ++q )
				if( q >= end || *q != *szWord )
					return Fail( "bad literal" );
			p = q;
			return true;
		}

		static int HexDigit( char c )
		{
			if( c >= '0' && c <= '9' )	return c - '0';
			if( c >= 'a' && c <= 'f' )	return c - 'a' + 10;
			if( c >= 'A' && c <= 'F' )	return c - 'A' + 10;
			return -1;
		}

		bool Hex4( unsigned &out )
		{
			if( end - p < 4 )
				return Fail( "short \\u escape" );
			out = 0;
			for( int i = 0; i < 4; ++i )
			{
				const int d = HexDigit( p[i] );
				if( d < 0 )
					return Fail( "bad \\u escape" );
				out = out * 16 + d;
			}
			p += 4;
			return true;
		}

		static void AppendUTF8( CString &s, unsigned c )
		{
			if( c < 0x80 )
				s += (char) c;
			else if( c < 0x800 )
			{
				s += (char) (0xC0 | (c >> 6));
				s += (char) (0x80 | (c & 0x3F));
			}
			else if( c < 0x10000 )
			{
				s += (char) (0xE0 | (c >> 12));
				s += (char) (0x80 | ((c >> 6) & 0x3F));
				s += (char) (0x80 | (c & 0x3F));
			}
			else
			{
				s += (char) (0xF0 | (c >> 18));
				s += (char) (0x80 | ((c >> 12) & 0x3F));
				s += (char) (0x80 | ((c >> 6) & 0x3F));
				s += (char) (0x80 | (c & 0x3F));
			}
		}

		bool String( CString &out )
		{
			++p;	// the quote
			out = "";
			while( p < end )
			{
				const char c = *p++;
				if( c == '"' )
					return true;
				if( (unsigned char) c < 0x20 )
					return Fail( "control character in a string" );
				if( c != '\\' )
				{
					out += c;
					continue;
				}
				if( p >= end )
					break;
				const char e = *p++;
				switch( e )
				{
				case '"':	out += '"';		break;
				case '\\':	out += '\\';	break;
				case '/':	out += '/';		break;
				case 'b':	out += '\b';	break;
				case 'f':	out += '\f';	break;
				case 'n':	out += '\n';	break;
				case 'r':	out += '\r';	break;
				case 't':	out += '\t';	break;
				case 'u':
					{
						unsigned c1;
						if( !Hex4(c1) )
							return false;
						if( c1 >= 0xD800 && c1 < 0xDC00 )
						{
							/* A surrogate pair. */
							unsigned c2;
							if( end - p < 2 || p[0] != '\\' || p[1] != 'u' )
								return Fail( "lone surrogate" );
							p += 2;
							if( !Hex4(c2) )
								return false;
							if( c2 < 0xDC00 || c2 >= 0xE000 )
								return Fail( "bad surrogate pair" );
							c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
						}
						else if( c1 >= 0xDC00 && c1 < 0xE000 )
							return Fail( "lone surrogate" );
						AppendUTF8( out, c1 );
					}
					break;
				default:
					return Fail( "bad escape" );
				}
			}
			return Fail( "unterminated string" );
		}

		bool Number( JsonValue &out )
		{
			const char *start = p;
			if( p < end && *p == '-' )
				++p;
			if( p >= end || *p < '0' || *p > '9' )
				return Fail( "bad number" );
			while( p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-') )
				++p;
			const CString sNum( start, p - start );
			char *pEnd;
			const double f = strtod( sNum.c_str(), &pEnd );
			if( *pEnd != '\0' || f != f || f > DBL_MAX || f < -DBL_MAX )
				return Fail( "bad number" );
			out = JsonValue::Number( f, sNum );
			return true;
		}

		bool Value( JsonValue &out, int iDepth )
		{
			if( iDepth > MAX_DEPTH )
				return Fail( "nested too deeply" );
			SkipSpace();
			if( p >= end )
				return Fail( "unexpected end" );
			switch( *p )
			{
			case 'n':	out = JsonValue();	return Literal( "null" );
			case 't':	out = JsonValue::Bool( true );	return Literal( "true" );
			case 'f':	out = JsonValue::Bool( false );	return Literal( "false" );
			case '"':
				{
					CString s;
					if( !String(s) )
						return false;
					out = JsonValue::String( s );
					return true;
				}
			case '[':
				{
					++p;
					out = JsonValue::Array();
					SkipSpace();
					if( p < end && *p == ']' )
					{
						++p;
						return true;
					}
					for(;;)
					{
						JsonValue v;
						if( !Value(v, iDepth + 1) )
							return false;
						out.Append( v );
						SkipSpace();
						if( p >= end )
							return Fail( "unterminated array" );
						if( *p == ']' )
						{
							++p;
							return true;
						}
						if( *p != ',' )
							return Fail( "expected , in an array" );
						++p;
					}
				}
			case '{':
				{
					++p;
					out = JsonValue::Object();
					SkipSpace();
					if( p < end && *p == '}' )
					{
						++p;
						return true;
					}
					for(;;)
					{
						SkipSpace();
						if( p >= end || *p != '"' )
							return Fail( "expected a member name" );
						CString sKey;
						if( !String(sKey) )
							return false;
						SkipSpace();
						if( p >= end || *p != ':' )
							return Fail( "expected :" );
						++p;
						JsonValue v;
						if( !Value(v, iDepth + 1) )
							return false;
						out.Set( sKey, v );
						SkipSpace();
						if( p >= end )
							return Fail( "unterminated object" );
						if( *p == '}' )
						{
							++p;
							return true;
						}
						if( *p != ',' )
							return Fail( "expected , in an object" );
						++p;
					}
				}
			default:
				return Number( out );
			}
		}
	};
}

bool JsonValue::Parse( const CString &s, JsonValue &out, CString &sError )
{
	Reader r( s );
	out = JsonValue();
	if( !r.Value(out, 0) )
	{
		sError = r.sError;
		out = JsonValue();
		return false;
	}
	r.SkipSpace();
	if( r.p != r.end )
	{
		sError = "text after the value";
		out = JsonValue();
		return false;
	}
	return true;
}
