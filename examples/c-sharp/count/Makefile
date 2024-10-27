
.PHONY: publish-osx publish-win help clean

help:
	@echo "make publish-win|publish-osx|clean"

publish-win:
	dotnet publish -c Release -r win-x64 --self-contained true -o ./publish/win-x64
	@echo "Remember to copy libzsv.dll into ./publish/win-x64"

publish-osx:
	dotnet publish -c Release -r osx-arm64 --self-contained true -o ./publish/osx-arm64
	@echo "Remember to copy libzsv.dylib into ./publish/win-x64"

clean:
	rm -rf obj bin publish
