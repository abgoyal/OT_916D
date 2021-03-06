

#import <JavaScriptCore/WebKitAvailability.h>

#if WEBKIT_VERSION_MAX_ALLOWED >= WEBKIT_VERSION_1_3

@class NSString;

extern NSString * const DOMException;

enum DOMExceptionCode {
    DOM_INDEX_SIZE_ERR                = 1,
    DOM_DOMSTRING_SIZE_ERR            = 2,
    DOM_HIERARCHY_REQUEST_ERR         = 3,
    DOM_WRONG_DOCUMENT_ERR            = 4,
    DOM_INVALID_CHARACTER_ERR         = 5,
    DOM_NO_DATA_ALLOWED_ERR           = 6,
    DOM_NO_MODIFICATION_ALLOWED_ERR   = 7,
    DOM_NOT_FOUND_ERR                 = 8,
    DOM_NOT_SUPPORTED_ERR             = 9,
    DOM_INUSE_ATTRIBUTE_ERR           = 10,
    DOM_INVALID_STATE_ERR             = 11,
    DOM_SYNTAX_ERR                    = 12,
    DOM_INVALID_MODIFICATION_ERR      = 13,
    DOM_NAMESPACE_ERR                 = 14,
    DOM_INVALID_ACCESS_ERR            = 15
};

#endif
