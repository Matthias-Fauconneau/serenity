http://mail.google.com/inbox?q=test#a
URL: (scheme ':')? "//"? host path? ('?' query)? ('#' hash)?
#(authorization '@')? needs unlimited lookahead | GLR to disambiguate from host
scheme: id
authorization: id
host: id ('.' id)+
path: ('/' id)+
query: id
hash: id
id: [a-zA-Z0-9"'!$&*_\-+=|,]+
