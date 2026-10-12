/* OnlineJson - Just enough JSON for the online server's API: a value that can
 * be read from text, looked at, built up and written back out.
 *
 * Objects keep their members in the order they were added, which keeps what we
 * send easy to read in logs and tests.  Numbers keep the text they were read
 * or made from, so a float written with %.9g goes out exactly as it came in.
 * Reading something that isn't there gives a null value, never a crash, so
 *
 *   int iRank = v["me"]["rank"].GetInt( 0 );
 *
 * is fine whatever the server sent. */

#ifndef ONLINE_JSON_H
#define ONLINE_JSON_H

class JsonValue
{
public:
	enum Type { TYPE_NULL, TYPE_BOOL, TYPE_NUMBER, TYPE_STRING, TYPE_ARRAY, TYPE_OBJECT, TYPE_RAW };

	JsonValue(): m_Type(TYPE_NULL), m_bBool(false), m_fNumber(0) { }

	static JsonValue Bool( bool b );
	static JsonValue Int( int i );
	/* With %.9g, so it reads back as the same float. */
	static JsonValue Float( float f );
	static JsonValue Double( double f );
	static JsonValue String( const CString &s );
	static JsonValue Array();
	static JsonValue Object();
	/* A number as it was written. */
	static JsonValue Number( double f, const CString &sText );
	/* Text that is already JSON, written out as it is. */
	static JsonValue Raw( const CString &sJson );

	Type GetType() const { return m_Type; }
	bool IsNull() const { return m_Type == TYPE_NULL; }
	bool IsObject() const { return m_Type == TYPE_OBJECT; }
	bool IsArray() const { return m_Type == TYPE_ARRAY; }
	bool IsString() const { return m_Type == TYPE_STRING; }
	bool IsNumber() const { return m_Type == TYPE_NUMBER; }

	/* With a fallback for anything of the wrong type. */
	bool GetBool( bool bDefault = false ) const;
	double GetNumber( double fDefault = 0 ) const;
	int GetInt( int iDefault = 0 ) const;
	CString GetString( const CString &sDefault = "" ) const;

	/* Arrays and objects. */
	unsigned size() const { return m_Children.size(); }
	const JsonValue &operator[]( unsigned i ) const;
	const JsonValue &operator[]( const char *szKey ) const;
	bool Has( const char *szKey ) const;
	const CString &GetKey( unsigned i ) const { return m_Keys[i]; }

	/* Adds a member to an object (replacing one with the same name) or an
	 * element to an array, and returns it. */
	JsonValue &Set( const CString &sKey, const JsonValue &v );
	JsonValue &Append( const JsonValue &v );

	CString Write() const;
	void Write( CString &sOut ) const;

	/* False with a reason if s isn't one JSON value. */
	static bool Parse( const CString &s, JsonValue &out, CString &sError );

private:
	Type m_Type;
	bool m_bBool;
	double m_fNumber;
	CString m_sText;	// a string, a number as written, or raw JSON
	vector<JsonValue> m_Children;
	vector<CString> m_Keys;	// for objects, the name of each child
};

/* s as a JSON string, quotes and all.  Text that isn't UTF-8 is taken byte by
 * byte as Latin-1, so the result is always valid JSON. */
CString JsonQuote( const CString &s );

/* A float with enough digits that it reads back as the same float, or null. */
CString JsonFloatText( float f );

#endif
