#Client-Server

Supports PUT and GET requests on files that are 40 characters in length exactly.

Caches up to 160 Mb worth of PUT request data in an LRU cache for quick retrieval.

Data is stored in a kvs using linear probing.

Implements a PATCH command that stores an alias to an key. A key can be an httpname or another alias. When updating an alias it makes sure that whatever the new path is somewhat valid i.e the key exists or is an httpname. Thread safe name lookup so that both a GET and a PUT can resolve a name at the same time.
