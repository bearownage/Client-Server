README.md for Assignment 4

Implements a PATCH command that stores an alias to an key. A key can be an httpname or another alias. When updating an alias it makes sure that 
whatever the new path is somewhat valid i.e the key exists or is an httpname. Thread safe name lookup so that both a GET and a PUT can resolve a name 
at the same time. Uses Linear probing and a similar kvs write from assignment 3. The name resolves detects loops or super long paths.
