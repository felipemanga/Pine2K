//!MENU-ENTRY:Convert to 565

// This script looks in each pine folder and converts all of the images into 565s
// Press Ctrl+Enter to run this script or use the menu.

let promises = [];
let hashes = {};
let imageCount = 0;

start();

function start(){
    (dir("pine-2k").filter( path=>path.indexOf(".") == -1))
    .forEach(project=>{
        promises.push(...(dir("pine-2k/" + project)||[])
            .filter( file=>/\.png$/i.test(file) )
            .map( (file) => {
            return readImage(`pine-2k/${project}/${file}`)
                .then( image => {
                let img = convert(image, 0, 0, image.width, image.height);
                fs.writeFileSync(
                    `${DATA.projectPath}/pine-2k/${project}/${file.replace(/\..*$/, "")}.565`,
                    Uint16Array.from(img)
                );
                return true;
                });
            }));
    });
    Promise.all(promises).then(_=>{
        log("Conversion complete!");
    })
    .catch(ex=>{
        log(ex);
    });
}

function convert(img, xb, yb, xe, ye){
    let len, bytes, data = img.data;
    let out = [xe - xb, ye - yb];

    for( let y=yb; y<ye; ++y ){
        for( let x=xb; x<xe; ++x ){
            let i = (y * img.width + x) * 4;
            let R = (data[i++] / 255.0 * 0x1F) | 0;
            let G = (data[i++] / 255.0 * 0x3F) | 0;
            let B = (data[i++] / 255.0 * 0x1F) | 0;
            let A = data[i++];
            if( A < 128 ){
                R = B = 0x1F;
                G = 0;
            }
            let C = (R<<11) + (G<<5) + B;
            out.push(C);
        }
    }

    return out;
}
