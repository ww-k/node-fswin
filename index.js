if (process.platform !== 'win32') {
    console.warn('this module can only run in win32');
    module.exports = null;
} else {
    var fswinModuleName;
    var versions = process.versions;
    var runtime;
    var version;
    if (versions.nw) {
        runtime = 'nw';
        version = versions.nw;
    } else if (versions.electron) {
        runtime = 'electron';
        version = versions.electron;
    } else {
        runtime = 'node';
        version = versions.node;
    }
    fswinModuleName = `fswin_${runtime}_${version}_${process.arch}.node`;

    try {
        module.exports = require(`./fswin_${runtime}_${version}_${process.arch}.node`);
    } catch (e) {
        if (e.code == 'MODULE_NOT_FOUND') {
            try {
                module.exports = require(`./fswin_${runtime}_${process.arch}.node`);
            } catch (e) {
                if (e.code == 'MODULE_NOT_FOUND') {
                    console.warn(`not support this in ${runtime} @ ${version}`);
                } else {
                    console.error(e);
                }
                module.exports = null;
            }
        } else {
            console.error(e);
            module.exports = null;
        }
    }
}