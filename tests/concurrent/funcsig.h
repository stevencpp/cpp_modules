#ifdef _MSC_VER
	#define FUNCSIG __FUNCSIG__
#else
	#define FUNCSIG __PRETTY_FUNCTION__
#endif