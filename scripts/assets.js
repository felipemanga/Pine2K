//!MENU-ENTRY:Convert Sprites
//!MENU-SHORTCUT:C-M-s

// This script looks in the "sprites" folder and converts all of the json files into sprites.h.
// Press Ctrl+Enter to run this script or use the menu.

const transparentIndex = 0;
const maxImages = 3000;

let promises = [];
let palette;
let hashes = {};
let acc = [];
let imageCount = 0;

function hash(str){
    str = '"' + str;
    let v = 5381;
    for(let i=0; i<str.length; ++i){
        v = ((v*31 >>> 0) + str.charCodeAt(i)) >>> 0;
    }
    return v;
}

APP.getPalette(pal=>{
    palette = pal;
    if(pal) start();
});

function start(){
    let images = (dir("assets")||[])
                 .filter( file=>/\.png$/i.test(file) );
    if(images.length > maxImages) images.length = maxImages;

    Promise.all(images.map( (file) => {
        return readImage(`assets/${file}`)
            .then( image => {
                let img = convert(image, 0, 0, image.width, image.height);
                let key = hash(file.replace(/\..*/g, ""));
                if(hashes[key]){
                    log("Collision: ", key, file, hashes[key]);
                } else {
                    // log(file, "=>", key, "@", acc.length);
                }
                hashes[key] = file;
                while(acc.length&3)acc.push(0);
                acc.push(key & 0xFF); key >>= 8;
                acc.push(key & 0xFF); key >>= 8;
                acc.push(key & 0xFF); key >>= 8;
                acc.push(key & 0xFF); key >>= 8;
                acc.push(0, 0);
                acc.push(...img);
                imageCount++;
                return true;
            });
    })).then(_=>{
        acc = [
            (imageCount >> 0)&0xFF,
            (imageCount >> 8)&0xFF,
            (imageCount >> 16)&0xFF,
            (imageCount >> 24)&0xFF,
            ...acc
        ];
        write("assets.bin", new Uint8Array(acc));
        write("assets.h", `
extern "C" {
    extern const char assets[];
}

__asm__(".global assets\\n.align\\nassets:\\n.incbin \\"assets.bin\\"");
`);
        log("Sprite conversion complete!");
    })
    .catch(ex=>{
        log(ex);
    });
}

function convert(img, xb, yb, xe, ye){
    let bpp = 8;
    let len, bytes, data = img.data;
    let ppb = 8 / bpp;
    let out = [xe - xb, ye - yb];
    let run = [], max = Math.min(palette.length, 1<<bpp);
    let PC = undefined, PCC = 0;

    for( let y=yb; y<ye; ++y ){
        run.length = 0;

        for( let x=xb; x<xe; ++x ){
            let i = (y * img.width + x) * 4;
            let closest = 0;
            let closestDist = Number.POSITIVE_INFINITY;
            let R = data[i++]|0;
            let G = data[i++]|0;
            let B = data[i++]|0;
            let C = (R<<16) + (G<<8) + B;
            let A = data[i++]|0;

            if (A < 128) {
                closest = transparentIndex;
            } else if(C === PC){
                closest = PCC;
            } else {

                for( let c=0; c<max; ++c ){
                    if( c == transparentIndex )
                        continue;
                    const ca = palette[c];
                    const PR = ca[0]|0;
                    const PG = ca[1]|0;
                    const PB = ca[2]|0;
        	        const dist = (R-PR)*(R-PR)
                        + (G-PG)*(G-PG)
                        + (B-PB)*(B-PB)
                    ;

                    if( dist < closestDist ){
                        closest = c;
                        closestDist = dist;
                    }
                }

                PC = C;
                PCC = closest;
            }

            let lx = x - xb;
            let shift = (ppb - 1 - lx%ppb) * bpp;
            run[(lx/ppb)|0] = (run[(lx/ppb)|0]||0) + (closest<<shift);
        }

        out.push(...run);
    }

    return out;
}
