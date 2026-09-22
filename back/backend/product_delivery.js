const crypto = require('crypto');
const jwt = require('jsonwebtoken');

const CUSTOMER_TAG_PREFIX = 'SHADOW_CUSTOMER_TAG_';
const CUSTOMER_TAG_PLACEHOLDER =
    Buffer.from(`${CUSTOMER_TAG_PREFIX}${'0'.repeat(32)}`, 'ascii');
const COMBAT_POLICY_FLOATS = Object.freeze([
    1.35, 260.0, 0.2, 0.5, 0.5, 0.42, 0.5, 40.0, 0.35, 7.5,
    0.6, 0.75, 0.05, 1.2, 0.08, 0.25, 0.13, 0.09, 0.76, 1.0,
    0.85, 120.0
]);
const COMBAT_POLICY_INTS = Object.freeze([
    200, 400, 4000, 45, 1500, 120, 16
]);

function requireValue(value, label) {
    if (!value)
        throw new Error(`${label} is required`);
}

function deriveCustomerTag(userId, hwid, watermarkSecret) {
    requireValue(userId, 'userId');
    requireValue(hwid, 'hwid');
    requireValue(watermarkSecret, 'watermarkSecret');
    const digest = crypto
        .createHmac('sha256', watermarkSecret)
        .update(`${userId}:${hwid}`)
        .digest('hex')
        .slice(0, 32);
    return `${CUSTOMER_TAG_PREFIX}${digest}`;
}

function deriveVariantId(input) {
    const { userId, hwid, variantSecret, variantCount } = input;
    requireValue(userId, 'userId');
    requireValue(hwid, 'hwid');
    requireValue(variantSecret, 'variantSecret');
    if (!Number.isInteger(variantCount) || variantCount < 2 || variantCount > 64)
        throw new Error('variantCount must be between 2 and 64');
    const digest = crypto
        .createHmac('sha256', variantSecret)
        .update(`variant:${userId}:${hwid}`)
        .digest();
    return digest.readUInt32LE(0) % variantCount;
}

function personalizeArtifact(rawArtifact, customerTag) {
    if (!Buffer.isBuffer(rawArtifact))
        throw new Error('rawArtifact must be a Buffer');
    if (Buffer.byteLength(customerTag, 'ascii') !== CUSTOMER_TAG_PLACEHOLDER.length)
        throw new Error('customerTag has an invalid length');
    const first = rawArtifact.indexOf(CUSTOMER_TAG_PLACEHOLDER);
    const second = rawArtifact.indexOf(CUSTOMER_TAG_PLACEHOLDER, first + 1);
    if (first < 0 || second >= 0)
        throw new Error('artifact must contain exactly one customer tag placeholder');
    const personalized = Buffer.from(rawArtifact);
    personalized.write(customerTag, first, 'ascii');
    return personalized;
}

function hashArtifact(artifact) {
    return crypto.createHash('sha256').update(artifact).digest('hex');
}

function hashHwid(hwid) {
    return crypto.createHash('sha256').update(hwid).digest('hex');
}

function deriveFeatureCanary(input) {
    const { userId, launchId, featureId, watermarkSecret } = input;
    requireValue(userId, 'userId');
    requireValue(launchId, 'launchId');
    requireValue(featureId, 'featureId');
    requireValue(watermarkSecret, 'watermarkSecret');
    return crypto
        .createHmac('sha256', watermarkSecret)
        .update(`feature:${userId}:${launchId}:${featureId}`)
        .digest('hex');
}

function watermarkFeatureSource(source, canary) {
    if (!Buffer.isBuffer(source))
        throw new Error('feature source must be a Buffer');
    const left = Number.parseInt(canary.slice(0, 8), 16) >>> 0;
    const right = Number.parseInt(canary.slice(8, 16), 16) >>> 0;
    const value = (left ^ right) >>> 0;
    const prefix = Buffer.from(
        `;(()=>{const a=${left},b=${right};` +
        `if(((a^b)>>>0)===${value})globalThis["__sc_${canary.slice(0, 16)}"]=a;})();\n`
    );
    const suffix = Buffer.from(`\n;void "${canary}";\n`);
    return Buffer.concat([prefix, source, suffix]);
}

function encryptFeatureBundle(source) {
    const key = crypto.randomBytes(32);
    const iv = crypto.randomBytes(12);
    const cipher = crypto.createCipheriv('aes-256-gcm', key, iv);
    const ciphertext = Buffer.concat([cipher.update(source), cipher.final()]);
    return {
        key: key.toString('hex'),
        payload: Buffer.concat([iv, ciphertext, cipher.getAuthTag()])
            .toString('base64')
    };
}

function decryptFeatureBundle(bundle) {
    const payload = Buffer.from(bundle.payload, 'base64');
    const decipher = crypto.createDecipheriv(
        'aes-256-gcm',
        Buffer.from(bundle.key, 'hex'),
        payload.subarray(0, 12));
    decipher.setAuthTag(payload.subarray(payload.length - 16));
    return Buffer.concat([
        decipher.update(payload.subarray(12, payload.length - 16)),
        decipher.final()
    ]);
}

function combatPolicyBuffer() {
    const output = Buffer.alloc(
        (COMBAT_POLICY_FLOATS.length + COMBAT_POLICY_INTS.length) * 4);
    let offset = 0;
    for (const value of COMBAT_POLICY_FLOATS) {
        output.writeFloatLE(value, offset);
        offset += 4;
    }
    for (const value of COMBAT_POLICY_INTS) {
        output.writeUInt32LE(value, offset);
        offset += 4;
    }
    return output;
}

function watermarkPolicy(policy, canary) {
    return Buffer.concat([
        policy,
        Buffer.from(`SHADOW_POLICY_CANARY_${canary}`, 'ascii')
    ]);
}

function issueLaunchTicket(input) {
    const {
        userId, productId, hwid, customerTag, integrityHash,
        launchId, variantId, jwtSecret, expiresIn = '15m'
    } = input;
    [
        [userId, 'userId'], [productId, 'productId'], [hwid, 'hwid'],
        [customerTag, 'customerTag'], [integrityHash, 'integrityHash'],
        [launchId, 'launchId'], [jwtSecret, 'jwtSecret']
    ].forEach(([value, label]) => requireValue(value, label));
    if (!Number.isInteger(variantId) || variantId < 0 || variantId > 63)
        throw new Error('variantId is invalid');
    return jwt.sign({
        uid: userId,
        scope: 'product',
        product_id: productId,
        hwid_hash: hashHwid(hwid),
        customer_tag: customerTag,
        integrity_hash: integrityHash,
        launch_id: launchId,
        variant_id: variantId
    }, jwtSecret, { expiresIn });
}

function verifyLaunchTicket(token, expected, jwtSecret) {
    requireValue(token, 'token');
    requireValue(jwtSecret, 'jwtSecret');
    const claims = jwt.verify(token, jwtSecret);
    if (claims.scope !== 'product')
        throw new Error('ticket scope is invalid');
    const checks = [
        ['product_id', expected.productId],
        ['customer_tag', expected.customerTag],
        ['integrity_hash', expected.integrityHash],
        ['launch_id', expected.launchId],
        ['variant_id', expected.variantId],
        ['hwid_hash', expected.hwid ? hashHwid(expected.hwid) : null]
    ];
    for (const [name, value] of checks) {
        if (value !== null && value !== undefined && claims[name] !== value)
            throw new Error(`ticket ${name} does not match`);
    }
    return claims;
}

module.exports = {
    CUSTOMER_TAG_PLACEHOLDER,
    combatPolicyBuffer,
    deriveCustomerTag,
    deriveFeatureCanary,
    deriveVariantId,
    decryptFeatureBundle,
    encryptFeatureBundle,
    hashArtifact,
    issueLaunchTicket,
    personalizeArtifact,
    watermarkFeatureSource,
    watermarkPolicy,
    verifyLaunchTicket
};
