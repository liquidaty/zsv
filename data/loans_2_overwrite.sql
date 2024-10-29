PRAGMA foreign_keys=OFF;
BEGIN TRANSACTION;
CREATE TABLE overwrites(row int, column int, value text);
INSERT INTO overwrites VALUES(10,4,'hello1');
INSERT INTO overwrites VALUES(10,2,'hello2');
INSERT INTO overwrites VALUES(8,3,'hello3');
INSERT INTO overwrites VALUES(8,15,'hello4');
INSERT INTO overwrites VALUES(8,99999,'never');
COMMIT;
