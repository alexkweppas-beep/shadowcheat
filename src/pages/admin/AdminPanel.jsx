import React, { useState, useEffect, useCallback, useRef } from 'react';
import Header from '../../components/Header';
import './AdminPanel.css';

function AdminLoginForm({ onLogin }) {
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);

  const handleSubmit = async (e) => {
    e.preventDefault();
    setLoading(true);
    setError('');
    try {
      const res = await fetch('/api/auth/login', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ username, password, isWeb: true })
      });
      const data = await res.json();
      if (!res.ok) { setError(data.error || 'Ошибка входа'); return; }
      if (!data.user?.id) { setError('Нет доступа'); return; }
      const check = await fetch('/api/admin/stats', {
        headers: { 'Authorization': `Bearer ${data.token}` }
      });
      if (!check.ok) { setError('Доступ запрещен. Аккаунт не является администратором.'); return; }
      onLogin(data.token);
    } catch (e) {
      setError('Ошибка сети');
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="admin-login">
      <form onSubmit={handleSubmit} className="admin-login__form">
        <h2 className="admin-login__title">Вход в панель</h2>
        <input type="text" placeholder="логин" value={username} onChange={e => setUsername(e.target.value)}
          className="admin-input" required />
        <input type="password" placeholder="••••••••" value={password} onChange={e => setPassword(e.target.value)}
          className="admin-input" required />
        {error && <p className="admin-login__error">{error}</p>}
        <button type="submit" disabled={loading} className="cta-fill admin-login__submit" data-state={loading ? 'loading' : undefined}>
          {loading ? 'Входим…' : 'Войти'}
        </button>
      </form>
    </div>
  );
}

// ─── helpers ───
function fmtDate(ts) {
  if (!ts) return '—';
  try { return new Date(ts.includes('Z') || ts.includes('+') ? ts : ts + ' UTC').toLocaleString('ru-RU'); }
  catch { return ts; }
}
function truncate(s, n = 20) {
  if (!s) return '—';
  return s.length > n ? s.slice(0, n) + '...' : s;
}
function SearchBar({ value, onChange, placeholder = 'Поиск...' }) {
  return (
    <input
      type="text"
      className="admin-input"
      style={{ maxWidth: 320, marginBottom: 0 }}
      placeholder={placeholder}
      value={value}
      onChange={e => onChange(e.target.value)}
    />
  );
}

export default function AdminPanel() {
  const [token, setToken] = useState(localStorage.getItem('adminToken') || '');
  const [activeTab, setActiveTab] = useState('users');

  const [users, setUsers] = useState([]);
  const [payments, setPayments] = useState([]);
  const [hwidBans, setHwidBans] = useState([]);
  const [ipBans, setIpBans] = useState([]);
  const [sessionBans, setSessionBans] = useState([]);
  const [loaderVersions, setLoaderVersions] = useState([]);
  const [loaderSessions, setLoaderSessions] = useState([]);

  // logs
  const [activeLogTab, setActiveLogTab] = useState('activity');
  const [logsActivity, setLogsActivity] = useState([]);
  const [logsDownloads, setLogsDownloads] = useState([]);
  const [logsSecurity, setLogsSecurity] = useState([]);
  const [logsAudit, setLogsAudit] = useState([]);
  const [logsErrors, setLogsErrors] = useState([]);
  const [logsRequests, setLogsRequests] = useState([]);
  const [banReports, setBanReports] = useState([]);
  const [searchBanReports, setSearchBanReports] = useState('');

  // per-tab search
  const [searchUsers, setSearchUsers] = useState('');
  const [searchLogs, setSearchLogs] = useState('');
  const [searchBlacklist, setSearchBlacklist] = useState('');
  const [searchSessions, setSearchSessions] = useState('');
  const [searchVersions, setSearchVersions] = useState('');

  // modals
  const [modal, setModal] = useState(null);
  const [activeUser, setActiveUser] = useState(null);
  const [userInfo, setUserInfo] = useState(null);
  const [userInfoLoading, setUserInfoLoading] = useState(false);

  // form states
  const [balanceInput, setBalanceInput] = useState('');
  const [subProduct, setSubProduct] = useState('external');
  const [subDays, setSubDays] = useState('30');
  const [mediaActive, setMediaActive] = useState(false);
  const [distributorActive, setDistributorActive] = useState(false);
  const [promoCode, setPromoCode] = useState('');
  const [promoPercent, setPromoPercent] = useState('15');
  const [promoEarned, setPromoEarned] = useState('0');
  const [keysProduct, setKeysProduct] = useState('external');
  const [keysDays, setKeysDays] = useState('30');
  const [keysAmount, setKeysAmount] = useState('10');
  const [generatedKeys, setGeneratedKeys] = useState([]);
  const [banReason, setBanReason] = useState('');
  const [blacklistHwid, setBlacklistHwid] = useState('');
  const [blacklistReason, setBlacklistReason] = useState('');
  const [blacklistIp, setBlacklistIp] = useState('');
  const [blacklistIpReason, setBlacklistIpReason] = useState('');
  const [blacklistSession, setBlacklistSession] = useState('');
  const [blacklistSessionReason, setBlacklistSessionReason] = useState('');
  const [blacklistActiveTab, setBlacklistActiveTab] = useState('hwid');
  const [versionId, setVersionId] = useState('');
  const [versionLabel, setVersionLabel] = useState('');
  const [giveAllProduct, setGiveAllProduct] = useState('external');
  const [giveAllDays, setGiveAllDays] = useState('1');
  const [giveAllMode, setGiveAllMode] = useState('active_only');

  const authFetch = useCallback(async (url, options = {}) => {
    if (!token) return null;
    const res = await fetch(url, {
      ...options,
      headers: { ...options.headers, 'Authorization': `Bearer ${token}` }
    });
    if (res.status === 401 || res.status === 403) {
      localStorage.removeItem('adminToken');
      setToken('');
      return null;
    }
    return res.json();
  }, [token]);

  const loadData = useCallback(async () => {
    if (!token) return;
    if (activeTab === 'users') {
      const data = await authFetch(`/api/admin/users${searchUsers ? `?search=${encodeURIComponent(searchUsers)}` : ''}`);
      if (data) setUsers(data.users || data);
    } else if (activeTab === 'payments') {
      const data = await authFetch('/api/admin/payments');
      if (data) setPayments(data);
    } else if (activeTab === 'logs') {
      const searchQ = searchLogs ? `?search=${encodeURIComponent(searchLogs)}` : '';
      if (activeLogTab === 'activity') {
        const d = await authFetch(`/api/admin/activity-log${searchQ}`);
        if (d) setLogsActivity(Array.isArray(d) ? d : []);
      } else if (activeLogTab === 'downloads') {
        const d = await authFetch(`/api/admin/download-log${searchQ}`);
        if (d) setLogsDownloads(Array.isArray(d) ? d : []);
      } else if (activeLogTab === 'security') {
        const d = await authFetch(`/api/admin/security-events${searchQ}`);
        if (d) setLogsSecurity(Array.isArray(d) ? d : []);
      } else if (activeLogTab === 'audit') {
        const d = await authFetch(`/api/admin/audit-log${searchQ}`);
        if (d) setLogsAudit(Array.isArray(d) ? d : []);
      } else if (activeLogTab === 'errors') {
        const d = await authFetch(`/api/admin/loader-errors${searchQ}`);
        if (d) setLogsErrors(Array.isArray(d) ? d : (d?.errors || []));
      } else if (activeLogTab === 'requests') {
        const d = await authFetch(`/api/admin/request-log${searchQ}`);
        if (d) setLogsRequests(Array.isArray(d) ? d : []);
      }
    } else if (activeTab === 'ban_reports') {
      const searchQ = searchBanReports ? `?search=${encodeURIComponent(searchBanReports)}` : '';
      const d = await authFetch(`/api/admin/security-events${searchQ}`);
      if (d) setBanReports((Array.isArray(d) ? d : []).filter(e => e.event_type?.startsWith('CLIENT_REPORT_')));
    } else if (activeTab === 'blacklist') {
      const bansH = await authFetch('/api/admin/hwid-bans');
      const bansI = await authFetch('/api/admin/blacklist/ip');
      const bansS = await authFetch('/api/admin/blacklist/session');
      if (bansH) setHwidBans(bansH);
      if (bansI) setIpBans(bansI);
      if (bansS) setSessionBans(bansS);
    } else if (activeTab === 'versions') {
      const vers = await authFetch('/api/admin/loader-versions');
      if (vers) setLoaderVersions(vers);
    } else if (activeTab === 'sessions') {
      const sess = await authFetch('/api/admin/loader-sessions');
      if (sess) setLoaderSessions(sess);
    }
  }, [token, activeTab, activeLogTab, searchUsers, searchLogs, authFetch]);

  useEffect(() => { loadData(); }, [token, activeTab, activeLogTab]);

  // debounced search
  const searchTimer = useRef(null);
  const triggerSearch = (setter, val) => {
    setter(val);
    clearTimeout(searchTimer.current);
    searchTimer.current = setTimeout(() => loadData(), 600);
  };

  // ─── User Info Modal ───
  const openUserInfo = async (user) => {
    setActiveUser(user);
    setModal('user_info');
    setUserInfo(null);
    setUserInfoLoading(true);
    try {
      const data = await authFetch(`/api/admin/users/${user.id}/info`);
      setUserInfo(data);
    } finally {
      setUserInfoLoading(false);
    }
  };

  const handleUpdateUser = async (userId, body = {}) => {
    try {
      await fetch(`/api/admin/users/${userId}`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json', 'Authorization': `Bearer ${token}` },
        body: JSON.stringify(body)
      });
      loadData(); setModal(null);
    } catch (e) { console.error(e); }
  };

  const handleAction = async (userId, action, body = {}) => {
    try {
      await fetch(`/api/admin/users/${userId}/${action}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'Authorization': `Bearer ${token}` },
        body: JSON.stringify(body)
      });
      loadData(); setModal(null);
    } catch (e) { console.error(e); }
  };

  const handleFreezeAll = async () => {
    if (!window.confirm('Вы уверены, что хотите заморозить подписки ВСЕМ пользователям? (для обновления)')) return;
    try {
      const res = await fetch('/api/admin/subscriptions/freeze-all', { method: 'POST', headers: { 'Authorization': `Bearer ${token}` } });
      const data = await res.json();
      if (res.ok) alert(`Заморожено подписок: ${data.affected}`);
      loadData();
    } catch (e) { alert('Ошибка'); }
  };

  const handleUnfreezeAll = async () => {
    if (!window.confirm('Разморозить все подписки, замороженные для обновления? (те, кто замораживал сам, останутся замороженными)')) return;
    try {
      const res = await fetch('/api/admin/subscriptions/unfreeze-all', { method: 'POST', headers: { 'Authorization': `Bearer ${token}` } });
      const data = await res.json();
      if (res.ok) alert(`Разморожено подписок: ${data.affected}`);
      loadData();
    } catch (e) { alert('Ошибка'); }
  };

  const handleGenerateKeys = async () => {
    const data = await authFetch('/api/admin/keys/generate', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ product_id: keysProduct, days: parseInt(keysDays), amount: parseInt(keysAmount) })
    });
    if (data) setGeneratedKeys(data.keys || []);
  };

  const handleGiveAll = async () => {
    if (!window.confirm(`Выдать ${giveAllDays} дней (${giveAllMode === 'all' ? 'ВСЕМ пользователям' : 'только тем у кого есть эта подписка'})?`)) return;
    try {
      const res = await fetch('/api/admin/subscriptions/add-all', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'Authorization': `Bearer ${token}` },
        body: JSON.stringify({ product_id: giveAllProduct, days: parseInt(giveAllDays), mode: giveAllMode })
      });
      const data = await res.json();
      if (res.ok) {
        alert(`Успешно. Затронуто подписок/пользователей: ${data.affected}`);
        setModal(null);
        loadData();
      } else alert(data.error);
    } catch (e) { alert('Ошибка сети'); }
  };

  const handleAddBlacklist = async () => {
    await authFetch('/api/admin/blacklist/hwid', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ hwid: blacklistHwid, reason: blacklistReason })
    });
    setModal(null); loadData();
  };

  const handleAddIpBan = async () => {
    await authFetch('/api/admin/blacklist/ip', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ip: blacklistIp, reason: blacklistIpReason })
    });
    setModal(null); loadData();
  };

  const handleAddSessionBan = async () => {
    await authFetch('/api/admin/blacklist/session', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ session_uuid: blacklistSession, reason: blacklistSessionReason })
    });
    setModal(null); loadData();
  };

  const handleRemoveBlacklist = async (hwid) => {
    await authFetch(`/api/admin/hwid-ban/${hwid}`, { method: 'DELETE' });
    loadData();
  };
  const handleRemoveIpBan = async (ip) => {
    await authFetch(`/api/admin/blacklist/ip/${encodeURIComponent(ip)}`, { method: 'DELETE' });
    loadData();
  };
  const handleRemoveSessionBan = async (uuid) => {
    await authFetch(`/api/admin/blacklist/session/${encodeURIComponent(uuid)}`, { method: 'DELETE' });
    loadData();
  };

  const handleAddVersion = async (e) => {
    e.preventDefault();
    await authFetch('/api/admin/loader-versions', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ version_id: versionId, label: versionLabel })
    });
    setModal(null); loadData();
  };

  const handleRevokeVersion = async (id) => {
    await authFetch(`/api/admin/loader-versions/${id}/revoke`, { method: 'POST' });
    loadData();
  };
  const handleRestoreVersion = async (id) => {
    await authFetch(`/api/admin/loader-versions/${id}/restore`, { method: 'POST' });
    loadData();
  };
  const handleRevokeSession = async (id) => {
    await authFetch(`/api/admin/loader-sessions/${id}/revoke`, { method: 'POST' });
    loadData();
  };

  const handleUploadLoader = async (e) => {
    const file = e.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = async (ev) => {
      try {
        const res = await fetch('/api/admin/upload-loader', {
          method: 'POST',
          headers: { 'Authorization': `Bearer ${token}`, 'Content-Type': 'application/octet-stream' },
          body: ev.target.result
        });
        const data = await res.json();
        if (res.ok) alert('Loader успешно загружен!');
        else alert('Ошибка загрузки: ' + (data.error || ''));
      } catch { alert('Ошибка сети при загрузке'); }
    };
    reader.readAsArrayBuffer(file);
  };

  const openModal = (type, user = null) => {
    setActiveUser(user);
    setModal(type);
    if (type === 'balance' && user) setBalanceInput(user.balance || 0);
    if (type === 'media' && user) {
      setMediaActive(!!user.media_access);
      setDistributorActive(!!user.is_distributor);
      setPromoCode(user.promocode || '');
      setPromoPercent(user.promo_percent || '15');
      setPromoEarned(user.promo_earned || '0');
    }
    if (type === 'keys') setGeneratedKeys([]);
    if (type === 'ban') setBanReason('');
    if (type === 'blacklist_add') { setBlacklistHwid(''); setBlacklistReason(''); }
    if (type === 'add_version') { setVersionId(''); setVersionLabel(''); }
  };

  const handleIssueToDistributor = async () => {
    await fetch('/api/admin/distributor/issue', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'Authorization': `Bearer ${token}` },
      body: JSON.stringify({ distributor_id: activeUser.id, product_id: keysProduct, days: parseInt(keysDays), count: parseInt(keysAmount) })
    });
    setModal(null);
    alert('Ключи выданы дистрибьютору!');
  };

  if (!token) return <AdminLoginForm onLogin={(tok) => { localStorage.setItem('adminToken', tok); setToken(tok); }} />;

  // ── filtered data ──
  const filteredSessions = (loaderSessions || []).filter(s =>
    !searchSessions ||
    (s.username || '').toLowerCase().includes(searchSessions.toLowerCase()) ||
    (s.hwid || '').toLowerCase().includes(searchSessions.toLowerCase()) ||
    (s.session_uuid || '').toLowerCase().includes(searchSessions.toLowerCase())
  );
  const filteredVersions = (loaderVersions || []).filter(v =>
    !searchVersions ||
    (v.version_id || '').toLowerCase().includes(searchVersions.toLowerCase()) ||
    (v.label || '').toLowerCase().includes(searchVersions.toLowerCase())
  );
  const filteredHwidBans = (hwidBans || []).filter(b =>
    !searchBlacklist || (b.hwid || '').toLowerCase().includes(searchBlacklist.toLowerCase())
  );
  const filteredIpBans = (ipBans || []).filter(b =>
    !searchBlacklist || (b.ip || '').toLowerCase().includes(searchBlacklist.toLowerCase())
  );
  const filteredSessionBans = (sessionBans || []).filter(b =>
    !searchBlacklist || (b.session_uuid || '').toLowerCase().includes(searchBlacklist.toLowerCase())
  );

  return (
    <div className="admin-page">
      <Header />
      <div className="container admin-container" style={{ marginTop: '40px' }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '24px' }}>
          <h1 className="admin-title" style={{ margin: 0 }}>Админ-Панель</h1>
          <div style={{ display: 'flex', gap: '8px' }}>
            <button className="btn-admin" style={{ background: '#8b5cf6', color: 'white' }} onClick={() => openModal('give_all')}>🎁 Выдать всем</button>
            <button className="btn-admin" style={{ background: '#2563eb', color: 'white' }} onClick={handleFreezeAll}>❄️ Заморозить все (Обнова)</button>
            <button className="btn-admin" style={{ background: '#059669', color: 'white' }} onClick={handleUnfreezeAll}>🔥 Разморозить все</button>
            <button className="btn-primary" onClick={() => openModal('keys')}>Генератор ключей</button>
          </div>
        </div>

        <div className="admin-tabs">
          {['users', 'payments', 'ban_reports', 'logs', 'blacklist', 'versions', 'sessions'].map(t => (
            <button key={t} className={`admin-tab${activeTab === t ? ' active' : ''}`} onClick={() => setActiveTab(t)}>
              {t === 'users' ? 'Пользователи' : t === 'payments' ? 'Платежи' : t === 'ban_reports' ? '🛡️ Бан-репорты' : t === 'logs' ? 'Логи' : t === 'blacklist' ? 'Черный список' : t === 'versions' ? 'Версии Loader' : t === 'sessions' ? 'Сессии Loader' : t}
            </button>
          ))}
        </div>

        {/* ─── USERS ─── */}
        {activeTab === 'users' && (
          <div className="admin-card">
            <div style={{ marginBottom: '16px' }}>
              <SearchBar value={searchUsers} onChange={v => triggerSearch(setSearchUsers, v)} placeholder="Поиск по нику, HWID..." />
            </div>
            <table className="admin-table">
              <thead>
                <tr>
                  <th>ID</th><th>Username</th><th>HWID</th><th>Баланс</th><th>Подписки</th><th>Статус</th><th>Действия</th>
                </tr>
              </thead>
              <tbody>
                {(users || []).map(u => (
                  <tr key={u.id}>
                    <td style={{ opacity: 0.5 }}>{u.id}</td>
                    <td style={{ fontWeight: 'bold' }}>{u.username}</td>
                    <td style={{ fontFamily: 'monospace', fontSize: '11px' }} title={u.hwid}>{u.hwid ? truncate(u.hwid, 18) : '—'}</td>
                    <td>{u.balance ?? 0} ₽</td>
                    <td>
                      {(u.subscriptions || []).filter(s => s.days_left > 0).map(s => (
                        <span key={s.id} className="admin-badge" style={{ marginRight: '4px', background: 'rgba(34,197,94,0.1)', color: '#22c55e' }}>
                          {s.product_id}: {s.days_left}ч
                        </span>
                      ))}
                    </td>
                    <td>
                      {u.banned
                        ? <span className="admin-badge banned" title={u.ban_reason}>Забанен</span>
                        : <span className="admin-badge active">Активен</span>}
                    </td>
                    <td>
                      <div style={{ display: 'flex', gap: '4px', flexWrap: 'wrap' }}>
                        <button className="btn-admin" onClick={() => openUserInfo(u)}>ℹ️ Инфо</button>
                        <button className="btn-admin" onClick={() => openModal('balance', u)}>₽</button>
                        <button className="btn-admin" onClick={() => openModal('sub', u)}>Sub</button>
                        <button className="btn-admin" onClick={() => openModal('media', u)}>Роли</button>
                        {u.is_distributor && <button className="btn-admin" onClick={() => openModal('distributor_issue', u)}>Keys</button>}
                        <button className="btn-admin" onClick={() => handleAction(u.id, 'hwid-reset')}>HWID↺</button>
                        <button className={u.banned ? 'btn-primary' : 'btn-danger'} onClick={() => u.banned ? handleAction(u.id, 'ban') : openModal('ban', u)}>
                          {u.banned ? 'Разбан' : 'Бан'}
                        </button>
                      </div>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}

        {/* ─── BAN REPORTS ─── */}
        {activeTab === 'ban_reports' && (() => {
          const REASON_MAP = {
            '1':'Debugger','2':'HW Breakpoint','3':'RDTSC Timing','4':'VM Detected',
            '5':'Bad Process','6':'Honeypot Patched','7':'Integrity Fail','8':'Hook Detected',
            '9':'HWID Spoofer','10':'Kernel Tamper','11':'Bad Parent','12':'ETW Patched',
            '13':'Env Heuristics','14':'Advanced Threat','255':'Manual'
          };
          const dangerCodes = new Set(['1','2','7','8','14']);
          return (
            <div className="admin-card">
              <div style={{ display:'flex', gap:'12px', marginBottom:'16px', alignItems:'center', flexWrap:'wrap' }}>
                <SearchBar
                  value={searchBanReports}
                  onChange={v => { setSearchBanReports(v); clearTimeout(searchTimer.current); searchTimer.current = setTimeout(() => loadData(), 600); }}
                  placeholder="Ник, HWID, IP, причина..."
                />
                <button className="btn-admin" onClick={loadData} style={{ marginLeft:'auto' }}>↻ Обновить</button>
              </div>
              {banReports.length === 0
                ? <div style={{ padding:'40px', textAlign:'center', color:'#555' }}>🛡️ Бан-репортов пока нет</div>
                : <table className="admin-table">
                    <thead><tr>
                      <th>ID</th><th>Пользователь</th><th>HWID</th><th>IP</th><th>Причина</th><th>Детали</th><th>Время</th><th>Действие</th>
                    </tr></thead>
                    <tbody>
                      {banReports.map(e => {
                        const code = e.event_type?.replace('CLIENT_REPORT_', '') ?? '';
                        const label = REASON_MAP[code] ?? code;
                        const isDanger = dangerCodes.has(code);
                        return (
                          <tr key={e.id}>
                            <td style={{ opacity:.5 }}>{e.id}</td>
                            <td style={{ fontWeight:'bold' }}>{e.username || <span style={{color:'#555'}}>—</span>}</td>
                            <td style={{ fontFamily:'monospace', fontSize:'11px' }} title={e.hwid}>{truncate(e.hwid, 18)}</td>
                            <td style={{ fontFamily:'monospace', fontSize:'11px' }}>{e.ip || '—'}</td>
                            <td>
                              <span className="admin-badge" style={{ background: isDanger ? 'rgba(239,68,68,.15)' : 'rgba(251,191,36,.12)', color: isDanger ? '#ef4444' : '#fbbf24' }}>
                                {label}
                              </span>
                            </td>
                            <td style={{ fontSize:'11px', maxWidth:220 }} title={e.detail}>{e.detail || '—'}</td>
                            <td style={{ whiteSpace:'nowrap' }}>{fmtDate(e.ts)}</td>
                            <td>
                              {e.user_id
                                ? <button className="btn-danger" style={{ padding:'3px 10px', fontSize:'12px' }}
                                    onClick={async () => {
                                      if (!window.confirm(`Забанить ${e.username || '#'+e.user_id}?\nПричина: ${e.event_type}`)) return;
                                      await authFetch(`/api/admin/users/${e.user_id}/ban`, { method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({ reason: e.event_type }) });
                                      loadData();
                                    }}>🚫 Бан</button>
                                : e.hwid
                                  ? <button className="btn-danger" style={{ padding:'3px 10px', fontSize:'12px' }}
                                      onClick={async () => {
                                        if (!window.confirm(`HWID бан: ${e.hwid?.slice(0,20)}?`)) return;
                                        await authFetch('/api/admin/blacklist/hwid', { method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({ hwid: e.hwid, reason: e.event_type }) });
                                        loadData();
                                      }}>🚫 HWID</button>
                                  : <span style={{ color:'#555', fontSize:'12px' }}>—</span>}
                            </td>
                          </tr>
                        );
                      })}
                    </tbody>
                  </table>
              }
            </div>
          );
        })()}

        {/* ─── PAYMENTS ─── */}
        {activeTab === 'payments' && (
          <div className="admin-card">
            <table className="admin-table">
              <thead>
                <tr><th>ID</th><th>Пользователь</th><th>Сумма</th><th>Метод</th><th>Статус</th><th>Дата</th></tr>
              </thead>
              <tbody>
                {payments.map(p => (
                  <tr key={p.id}>
                    <td>{p.id}</td>
                    <td>{p.username || p.user_id}</td>
                    <td>{p.amount_rub} ₽</td>
                    <td>{p.method}</td>
                    <td>
                      <span className={`admin-badge${p.status === 'completed' ? ' active' : p.status === 'pending' ? '' : ' banned'}`}>{p.status}</span>
                    </td>
                    <td>{fmtDate(p.created_at)}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}

        {/* ─── BLACKLIST ─── */}
        {activeTab === 'blacklist' && (
          <div className="admin-card">
            <div style={{ display: 'flex', gap: '8px', marginBottom: '16px', alignItems: 'center', flexWrap: 'wrap' }}>
              <div className="admin-tabs" style={{ border: 'none', marginBottom: 0 }}>
                {['hwid', 'ip', 'session'].map(t => (
                  <button key={t} className={`admin-tab${blacklistActiveTab === t ? ' active' : ''}`} onClick={() => setBlacklistActiveTab(t)}>
                    {t === 'hwid' ? 'HWID' : t === 'ip' ? 'IP адрес' : 'Сессия (UUID)'}
                  </button>
                ))}
              </div>
              <SearchBar value={searchBlacklist} onChange={setSearchBlacklist} placeholder="Поиск..." />
              <button className="btn-primary" onClick={() => {
                if (blacklistActiveTab === 'hwid') openModal('blacklist_add');
                else if (blacklistActiveTab === 'ip') openModal('blacklist_ip_add');
                else openModal('blacklist_session_add');
              }}>+ Добавить</button>
            </div>

            {blacklistActiveTab === 'hwid' && (
              <table className="admin-table">
                <thead><tr><th>HWID</th><th>Причина</th><th>User ID</th><th>Дата</th><th>Действия</th></tr></thead>
                <tbody>
                  {filteredHwidBans.map(b => (
                    <tr key={b.id}>
                      <td style={{ fontFamily: 'monospace', fontSize: '11px', maxWidth: 280 }} title={b.hwid}>{b.hwid}</td>
                      <td>{b.reason || '—'}</td>
                      <td>{b.user_id || '—'}</td>
                      <td>{fmtDate(b.banned_at)}</td>
                      <td><button className="btn-danger" onClick={() => handleRemoveBlacklist(b.hwid)}>Удалить</button></td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}

            {blacklistActiveTab === 'ip' && (
              <table className="admin-table">
                <thead><tr><th>IP адрес</th><th>Причина</th><th>Дата</th><th>Действия</th></tr></thead>
                <tbody>
                  {filteredIpBans.map((b, i) => (
                    <tr key={i}>
                      <td style={{ fontFamily: 'monospace' }}>{b.ip}</td>
                      <td>{b.reason || '—'}</td>
                      <td>{fmtDate(b.created_at)}</td>
                      <td><button className="btn-danger" onClick={() => handleRemoveIpBan(b.ip)}>Удалить</button></td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}

            {blacklistActiveTab === 'session' && (
              <table className="admin-table">
                <thead><tr><th>Session UUID</th><th>Причина</th><th>User ID</th><th>Дата</th><th>Действия</th></tr></thead>
                <tbody>
                  {filteredSessionBans.map((b, i) => (
                    <tr key={i}>
                      <td style={{ fontFamily: 'monospace', fontSize: '11px' }} title={b.session_uuid}>{b.session_uuid}</td>
                      <td>{b.reason || '—'}</td>
                      <td>{b.user_id || '—'}</td>
                      <td>{fmtDate(b.created_at)}</td>
                      <td><button className="btn-danger" onClick={() => handleRemoveSessionBan(b.session_uuid)}>Удалить</button></td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}
          </div>
        )}

        {/* ─── VERSIONS ─── */}
        {activeTab === 'versions' && (
          <div className="admin-card">
            <div style={{ marginBottom: '16px', display: 'flex', gap: '16px', alignItems: 'center', flexWrap: 'wrap' }}>
              <button className="btn-primary" onClick={() => openModal('add_version')}>+ Добавить версию</button>
              <label className="btn-primary" style={{ cursor: 'pointer', background: '#3b82f6', border: 'none' }}>
                ⬆️ Загрузить loader.exe
                <input type="file" accept=".exe" style={{ display: 'none' }} onChange={handleUploadLoader} />
              </label>
              <SearchBar value={searchVersions} onChange={setSearchVersions} placeholder="Поиск по версии..." />
            </div>
            <table className="admin-table">
              <thead>
                <tr><th>Version ID</th><th>Label</th><th>Token (первые 16)</th><th>Скачиваний</th><th>Статус</th><th>Дата</th><th>Действия</th></tr>
              </thead>
              <tbody>
                {filteredVersions.map(v => (
                  <tr key={v.id}>
                    <td style={{ fontWeight: 'bold' }}>{v.version_id}</td>
                    <td>{v.label || '—'}</td>
                    <td style={{ fontFamily: 'monospace', fontSize: '11px' }}>{v.version_token?.slice(0, 16)}...</td>
                    <td>{v.download_count ?? 0}</td>
                    <td>
                      {v.revoked
                        ? <span className="admin-badge banned">Отозвана</span>
                        : <span className="admin-badge" style={{ background: 'rgba(34,197,94,0.1)', color: '#22c55e' }}>Активна</span>}
                    </td>
                    <td>{fmtDate(v.created_at)}</td>
                    <td>
                      {v.revoked
                        ? <button className="btn-admin" onClick={() => handleRestoreVersion(v.id)}>Восстановить</button>
                        : <button className="btn-danger" onClick={() => handleRevokeVersion(v.id)}>Отозвать</button>}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}

        {/* ─── SESSIONS ─── */}
        {activeTab === 'sessions' && (
          <div className="admin-card">
            <div style={{ marginBottom: '16px' }}>
              <SearchBar value={searchSessions} onChange={setSearchSessions} placeholder="Поиск по нику, HWID, UUID..." />
            </div>
            <table className="admin-table">
              <thead>
                <tr><th>ID</th><th>Пользователь</th><th>HWID</th><th>Версия</th><th>Session UUID</th><th>IP</th><th>Статус</th><th>Создан</th><th>Действия</th></tr>
              </thead>
              <tbody>
                {filteredSessions.map(s => (
                  <tr key={s.id}>
                    <td style={{ opacity: 0.5 }}>{s.id}</td>
                    <td>{s.username || s.user_id}</td>
                    <td style={{ fontFamily: 'monospace', fontSize: '11px' }} title={s.hwid}>{truncate(s.hwid, 16)}</td>
                    <td>{s.version_id || '—'}</td>
                    <td style={{ fontFamily: 'monospace', fontSize: '11px' }} title={s.session_uuid}>{s.session_uuid || '—'}</td>
                    <td style={{ fontFamily: 'monospace', fontSize: '11px' }}>{s.ip || '—'}</td>
                    <td>
                      {s.revoked
                        ? <span className="admin-badge banned">Отозвана</span>
                        : <span className="admin-badge active">Активна</span>}
                    </td>
                    <td>{fmtDate(s.created_at)}</td>
                    <td>
                      {!s.revoked && <button className="btn-danger" onClick={() => handleRevokeSession(s.id)}>Отозвать</button>}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}

        {/* ─── LOGS ─── */}
        {activeTab === 'logs' && (
          <div className="admin-card">
            <div style={{ display: 'flex', gap: '8px', marginBottom: '16px', alignItems: 'center', flexWrap: 'wrap' }}>
              <div className="admin-tabs" style={{ border: 'none', marginBottom: 0 }}>
                {[
                  ['activity', 'Действия юзеров'],
                  ['requests', '🌐 Все запросы'],
                  ['downloads', 'Скачивания'],
                  ['security', 'Security Events'],
                  ['audit', 'Аудит Админов'],
                  ['errors', 'Ошибки Loader'],
                ].map(([k, label]) => (
                  <button key={k} className={`admin-tab${activeLogTab === k ? ' active' : ''}`} onClick={() => setActiveLogTab(k)}>{label}</button>
                ))}
              </div>
              <SearchBar value={searchLogs} onChange={v => triggerSearch(setSearchLogs, v)} placeholder="Поиск..." />
              <button className="btn-admin" onClick={loadData} style={{ marginLeft: 'auto' }}>↻ Обновить</button>
            </div>

            {/* Request Log (все HTTP запросы) */}
            {activeLogTab === 'requests' && (
              <table className="admin-table">
                <thead><tr>
                  <th>ID</th><th>Метод</th><th>Путь</th><th>IP</th><th>Пользователь</th>
                  <th>Статус</th><th>Параметры</th><th>User-Agent</th><th>Время</th>
                </tr></thead>
                <tbody>
                  {logsRequests.map(l => (
                    <tr key={l.id} style={{ background: l.status >= 400 ? 'rgba(239,68,68,0.06)' : l.status >= 200 && l.status < 300 ? 'transparent' : 'rgba(251,191,36,0.04)' }}>
                      <td style={{ opacity: 0.5 }}>{l.id}</td>
                      <td style={{ fontWeight: 'bold', color: l.method === 'POST' ? '#60a5fa' : l.method === 'DELETE' ? '#ef4444' : '#a3a3a3', whiteSpace: 'nowrap' }}>{l.method}</td>
                      <td style={{ fontFamily: 'monospace', fontSize: '11px', maxWidth: 200 }}>{l.path}</td>
                      <td style={{ fontFamily: 'monospace', fontSize: '11px' }}>{l.ip || '—'}</td>
                      <td>{l.username || (l.user_id ? `#${l.user_id}` : <span style={{color:'#555'}}>аноним</span>)}</td>
                      <td>
                        <span style={{ fontWeight: 'bold', color: l.status >= 500 ? '#ef4444' : l.status >= 400 ? '#f97316' : l.status >= 200 && l.status < 300 ? '#22c55e' : '#a3a3a3' }}>
                          {l.status || '—'}
                        </span>
                      </td>
                      <td style={{ fontFamily: 'monospace', fontSize: '11px', maxWidth: 180, wordBreak: 'break-all' }}>{l.body_keys || '—'}</td>
                      <td style={{ fontSize: '10px', maxWidth: 150, opacity: 0.6 }}>{truncate(l.user_agent, 30)}</td>
                      <td style={{ whiteSpace: 'nowrap' }}>{fmtDate(l.ts)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}

            {/* Activity Log */}
            {activeLogTab === 'activity' && (
              <table className="admin-table">
                <thead><tr><th>ID</th><th>Пользователь</th><th>Действие</th><th>Детали</th><th>HWID</th><th>IP</th><th>Версия</th><th>Session UUID</th><th>Время</th></tr></thead>
                <tbody>
                  {logsActivity.map(l => (
                    <tr key={l.id}>
                      <td style={{ opacity: 0.5 }}>{l.id}</td>
                      <td>{l.username || l.user_id || '—'}</td>
                      <td style={{ color: '#a78bfa', fontWeight: 'bold', whiteSpace: 'nowrap' }}>{l.action}</td>
                      <td style={{ fontSize: '12px', maxWidth: 200 }}>{l.detail || '—'}</td>
                      <td style={{ fontFamily: 'monospace', fontSize: '11px' }} title={l.hwid}>{truncate(l.hwid, 14)}</td>
                      <td style={{ fontFamily: 'monospace', fontSize: '11px' }}>{l.ip || '—'}</td>
                      <td style={{ fontFamily: 'monospace', fontSize: '11px' }}>{l.loader_version || '—'}</td>
                      <td style={{ fontFamily: 'monospace', fontSize: '11px' }} title={l.session_uuid}>{l.session_uuid ? truncate(l.session_uuid, 16) : '—'}</td>
                      <td style={{ whiteSpace: 'nowrap' }}>{fmtDate(l.ts)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}

            {/* Downloads */}
            {activeLogTab === 'downloads' && (
              <table className="admin-table">
                <thead><tr><th>ID</th><th>Пользователь</th><th>Продукт</th><th>HWID</th><th>Файл</th><th>IP</th><th>Время</th></tr></thead>
                <tbody>
                  {logsDownloads.map(l => (
                    <tr key={l.id}>
                      <td style={{ opacity: 0.5 }}>{l.id}</td>
                      <td>{l.username || l.user_id}</td>
                      <td>{l.product_id}</td>
                      <td style={{ fontFamily: 'monospace', fontSize: '11px' }} title={l.hwid}>{truncate(l.hwid, 16)}</td>
                      <td style={{ fontSize: '12px' }}>{l.loader_filename}</td>
                      <td style={{ fontFamily: 'monospace', fontSize: '11px' }}>{l.ip || '—'}</td>
                      <td style={{ whiteSpace: 'nowrap' }}>{fmtDate(l.ts)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}

            {/* Security Events */}
            {activeLogTab === 'security' && (
              <table className="admin-table">
                <thead><tr><th>ID</th><th>Пользователь</th><th>Событие</th><th>Детали</th><th>HWID</th><th>IP</th><th>Время</th></tr></thead>
                <tbody>
                  {logsSecurity.map(l => (
                    <tr key={l.id}>
                      <td style={{ opacity: 0.5 }}>{l.id}</td>
                      <td>{l.username || l.user_id || '—'}</td>
                      <td style={{ color: '#ef4444', fontWeight: 'bold', whiteSpace: 'nowrap' }}>{l.event_type}</td>
                      <td style={{ fontSize: '12px', maxWidth: 220 }}>{l.detail || '—'}</td>
                      <td style={{ fontFamily: 'monospace', fontSize: '11px' }} title={l.hwid}>{truncate(l.hwid, 14)}</td>
                      <td style={{ fontFamily: 'monospace', fontSize: '11px' }}>{l.ip || '—'}</td>
                      <td style={{ whiteSpace: 'nowrap' }}>{fmtDate(l.ts)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}

            {/* Audit */}
            {activeLogTab === 'audit' && (
              <table className="admin-table">
                <thead><tr><th>ID</th><th>Админ</th><th>Действие</th><th>Target ID</th><th>IP</th><th>Время</th></tr></thead>
                <tbody>
                  {logsAudit.map(l => (
                    <tr key={l.id}>
                      <td style={{ opacity: 0.5 }}>{l.id}</td>
                      <td>{l.admin_name || l.admin_id}</td>
                      <td style={{ color: '#6366f1', fontWeight: 'bold' }}>{l.action}</td>
                      <td>{l.target_id}</td>
                      <td style={{ fontFamily: 'monospace', fontSize: '11px' }}>{l.ip || '—'}</td>
                      <td style={{ whiteSpace: 'nowrap' }}>{fmtDate(l.ts)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}

            {/* Loader Errors */}
            {activeLogTab === 'errors' && (
              <table className="admin-table">
                <thead><tr><th>ID</th><th>Пользователь</th><th>Фаза</th><th>Сообщение</th><th>HWID</th><th>Версия</th><th>IP</th><th>Время</th><th></th></tr></thead>
                <tbody>
                  {logsErrors.map(l => (
                    <tr key={l.id}>
                      <td style={{ opacity: 0.5 }}>{l.id}</td>
                      <td>{l.username || l.user_id || 'anon'}</td>
                      <td style={{ color: '#f97316', fontWeight: 'bold' }}>{l.phase}</td>
                      <td style={{ fontSize: '11px', maxWidth: 260, wordBreak: 'break-all' }}>{l.message}</td>
                      <td style={{ fontFamily: 'monospace', fontSize: '11px' }} title={l.hwid}>{truncate(l.hwid, 14)}</td>
                      <td style={{ fontFamily: 'monospace', fontSize: '11px' }}>{l.loader_ver || '—'}</td>
                      <td style={{ fontFamily: 'monospace', fontSize: '11px' }}>{l.ip || '—'}</td>
                      <td style={{ whiteSpace: 'nowrap' }}>{fmtDate(l.ts)}</td>
                      <td><button className="btn-danger" style={{ padding: '2px 8px', fontSize: '11px' }} onClick={async () => { await authFetch(`/api/admin/loader-errors/${l.id}`, { method: 'DELETE' }); loadData(); }}>✕</button></td>
                    </tr>
                  ))}
                </tbody>
              </table>
            )}
          </div>
        )}
      </div>

      {/* ─── MODALS ─── */}
      {modal && (
        <div className="admin-modal-overlay" onClick={() => setModal(null)}>
          <div className="admin-modal" onClick={e => e.stopPropagation()} style={{ maxHeight: '90vh', overflowY: 'auto' }}>

            {/* USER INFO */}
            {modal === 'user_info' && (
              <div style={{ minWidth: 520, maxWidth: 700 }}>
                <h3 className="admin-modal-title">ℹ️ Инфо о пользователе: {activeUser?.username}</h3>
                {userInfoLoading && <p style={{ color: '#888', textAlign: 'center' }}>Загрузка...</p>}
                {userInfo && (
                  <div style={{ display: 'flex', flexDirection: 'column', gap: '12px' }}>
                    {/* Base info */}
                    <div style={{ background: '#111', borderRadius: 8, padding: '12px 16px' }}>
                      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '8px', fontSize: '13px' }}>
                        <div><span style={{ color: '#888' }}>ID:</span> {userInfo.id}</div>
                        <div><span style={{ color: '#888' }}>Username:</span> {userInfo.username}</div>
                        <div><span style={{ color: '#888' }}>Баланс:</span> {userInfo.balance} ₽</div>
                        <div><span style={{ color: '#888' }}>Создан:</span> {fmtDate(userInfo.created_at)}</div>
                        <div><span style={{ color: '#888' }}>Статус:</span> {userInfo.banned ? <span style={{ color: '#ef4444' }}>Забанен ({userInfo.ban_reason})</span> : <span style={{ color: '#22c55e' }}>Активен</span>}</div>
                        <div><span style={{ color: '#888' }}>Роли:</span> {[userInfo.is_admin && 'Админ', userInfo.media_access && 'Медиа', userInfo.is_distributor && 'Дистриб.'].filter(Boolean).join(', ') || 'Нет'}</div>
                      </div>
                    </div>

                    {/* HWID */}
                    <div style={{ background: '#111', borderRadius: 8, padding: '12px 16px', fontSize: '12px' }}>
                      <div style={{ color: '#888', marginBottom: 4, fontSize: '11px', textTransform: 'uppercase', letterSpacing: '0.05em' }}>HWID</div>
                      <div style={{ fontFamily: 'monospace', wordBreak: 'break-all', color: userInfo.hwid ? '#e2e8f0' : '#555' }}>{userInfo.hwid || 'Не привязан'}</div>
                      {userInfo.hwid_locked ? <div style={{ color: '#f97316', fontSize: '11px', marginTop: 4 }}>🔒 Заблокирован</div> : null}
                    </div>

                    {/* Loader Session & Binding */}
                    <div style={{ background: '#111', borderRadius: 8, padding: '12px 16px', fontSize: '12px' }}>
                      <div style={{ color: '#888', marginBottom: 6, fontSize: '11px', textTransform: 'uppercase', letterSpacing: '0.05em' }}>Текущая сессия лоадера</div>
                      {userInfo.loader_binding ? (
                        <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '6px' }}>
                          <div><span style={{ color: '#888' }}>Session UUID:</span><br /><span style={{ fontFamily: 'monospace', fontSize: '11px', wordBreak: 'break-all' }}>{userInfo.loader_binding.session_uuid || '—'}</span></div>
                          <div><span style={{ color: '#888' }}>Версия:</span> {userInfo.loader_binding.version_id || '—'}</div>
                          <div><span style={{ color: '#888' }}>IP скачивания:</span> <span style={{ fontFamily: 'monospace' }}>{userInfo.loader_binding.ip || '—'}</span></div>
                          <div><span style={{ color: '#888' }}>Дата скачивания:</span> {fmtDate(userInfo.loader_binding.created_at)}</div>
                        </div>
                      ) : <div style={{ color: '#555' }}>Нет активной сессии</div>}
                      {userInfo.loader_session?.last_heartbeat && (
                        <div style={{ marginTop: 6, color: '#888', fontSize: '11px' }}>Последний heartbeat: {fmtDate(userInfo.loader_session.last_heartbeat)}</div>
                      )}
                    </div>

                    {/* Last logins */}
                    <div style={{ background: '#111', borderRadius: 8, padding: '12px 16px', fontSize: '12px' }}>
                      <div style={{ color: '#888', marginBottom: 6, fontSize: '11px', textTransform: 'uppercase', letterSpacing: '0.05em' }}>Последние входы</div>
                      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '8px' }}>
                        <div>
                          <div style={{ color: '#888', marginBottom: 2 }}>🖥 Лоадер</div>
                          {userInfo.last_loader_login ? (
                            <>
                              <div>IP: <span style={{ fontFamily: 'monospace' }}>{userInfo.last_loader_login.ip || '—'}</span></div>
                              <div>Версия: {userInfo.last_loader_login.loader_version || '—'}</div>
                              <div>UUID: <span style={{ fontFamily: 'monospace', fontSize: '10px' }}>{userInfo.last_loader_login.session_uuid || '—'}</span></div>
                              <div style={{ color: '#888', fontSize: '11px' }}>{fmtDate(userInfo.last_loader_login.ts)}</div>
                            </>
                          ) : <div style={{ color: '#555' }}>Нет данных</div>}
                        </div>
                        <div>
                          <div style={{ color: '#888', marginBottom: 2 }}>🌐 Сайт</div>
                          {userInfo.last_web_login ? (
                            <>
                              <div>IP: <span style={{ fontFamily: 'monospace' }}>{userInfo.last_web_login.ip || '—'}</span></div>
                              <div style={{ color: '#888', fontSize: '11px' }}>{fmtDate(userInfo.last_web_login.ts)}</div>
                            </>
                          ) : <div style={{ color: '#555' }}>Нет данных</div>}
                        </div>
                      </div>
                    </div>

                    {/* Subscriptions */}
                    {userInfo.subscriptions?.length > 0 && (
                      <div style={{ background: '#111', borderRadius: 8, padding: '12px 16px', fontSize: '12px' }}>
                        <div style={{ color: '#888', marginBottom: 6, fontSize: '11px', textTransform: 'uppercase', letterSpacing: '0.05em' }}>Подписки</div>
                        {userInfo.subscriptions.map(s => (
                          <div key={s.id} style={{ display: 'flex', gap: '12px', padding: '4px 0', borderBottom: '1px solid #1e1e1e' }}>
                            <span style={{ color: '#a78bfa' }}>{s.product_id}</span>
                            <span>{s.days_left} ч ({Math.ceil(s.days_left/24)} дн)</span>
                            {s.is_frozen ? <span style={{ color: '#f97316' }}>❄️ Заморожена</span> : null}
                          </div>
                        ))}
                      </div>
                    )}

                    {/* Recent Activity */}
                    {userInfo.recent_activity?.length > 0 && (
                      <div style={{ background: '#111', borderRadius: 8, padding: '12px 16px', fontSize: '12px' }}>
                        <div style={{ color: '#888', marginBottom: 6, fontSize: '11px', textTransform: 'uppercase', letterSpacing: '0.05em' }}>Последние действия</div>
                        <table style={{ width: '100%', borderCollapse: 'collapse' }}>
                          <thead><tr style={{ color: '#555', fontSize: '11px' }}><th style={{ textAlign: 'left' }}>Действие</th><th style={{ textAlign: 'left' }}>IP</th><th style={{ textAlign: 'left' }}>Версия</th><th style={{ textAlign: 'left' }}>Время</th></tr></thead>
                          <tbody>
                            {userInfo.recent_activity.map((a, i) => (
                              <tr key={i} style={{ borderBottom: '1px solid #1a1a1a' }}>
                                <td style={{ padding: '3px 0', color: '#a78bfa' }}>{a.action}</td>
                                <td style={{ fontFamily: 'monospace', fontSize: '11px' }}>{a.ip || '—'}</td>
                                <td style={{ fontFamily: 'monospace', fontSize: '11px' }}>{a.loader_version || '—'}</td>
                                <td style={{ color: '#888', fontSize: '11px' }}>{fmtDate(a.ts)}</td>
                              </tr>
                            ))}
                          </tbody>
                        </table>
                      </div>
                    )}

                    {/* Security events */}
                    {userInfo.security_events?.length > 0 && (
                      <div style={{ background: '#111', borderRadius: 8, padding: '12px 16px', fontSize: '12px' }}>
                        <div style={{ color: '#888', marginBottom: 6, fontSize: '11px', textTransform: 'uppercase', letterSpacing: '0.05em' }}>Security Events (последние 5)</div>
                        {userInfo.security_events.map((e, i) => (
                          <div key={i} style={{ padding: '4px 0', borderBottom: '1px solid #1a1a1a', display: 'flex', gap: '12px' }}>
                            <span style={{ color: '#ef4444', minWidth: 120 }}>{e.event_type}</span>
                            <span style={{ color: '#888', fontSize: '11px' }}>{e.detail}</span>
                            <span style={{ fontFamily: 'monospace', fontSize: '11px', color: '#888', marginLeft: 'auto' }}>{e.ip}</span>
                          </div>
                        ))}
                      </div>
                    )}

                    <div className="admin-modal-actions">
                      <button className="btn-admin" onClick={() => setModal(null)}>Закрыть</button>
                      <button className="btn-admin" onClick={() => { setModal(null); openModal('balance', activeUser); }}>₽ Баланс</button>
                      <button className="btn-admin" onClick={() => { setModal(null); openModal('sub', activeUser); }}>Sub</button>
                      <button className="btn-admin" onClick={() => { handleAction(activeUser.id, 'hwid-reset'); setModal(null); }}>HWID↺</button>
                      <button className={activeUser?.banned ? 'btn-primary' : 'btn-danger'}
                        onClick={() => { setModal(null); activeUser?.banned ? handleAction(activeUser.id, 'ban') : openModal('ban', activeUser); }}>
                        {activeUser?.banned ? 'Разбан' : 'Бан'}
                      </button>
                    </div>
                  </div>
                )}
              </div>
            )}

            {modal === 'balance' && (
              <>
                <h3 className="admin-modal-title">Изменить баланс: {activeUser?.username}</h3>
                <input type="number" className="admin-input" value={balanceInput} onChange={e => setBalanceInput(e.target.value)} />
                <div className="admin-modal-actions">
                  <button className="btn-admin" onClick={() => setModal(null)}>Отмена</button>
                  <button className="btn-primary" onClick={() => handleAction(activeUser.id, 'balance', { balance: balanceInput })}>Сохранить</button>
                </div>
              </>
            )}

            {modal === 'sub' && (
              <>
                <h3 className="admin-modal-title">Управление подпиской: {activeUser?.username}</h3>
                <select className="admin-input" value={subProduct} onChange={e => setSubProduct(e.target.value)}>
                  <option value="external">EXTERNAL</option>
                  <option value="trigger">TRIGGER</option>
                  <option value="trigger_silent">TRIGGER + SILENT</option>
                  <option value="ragemp">RAGE:MP</option>
                </select>
                <label style={{ fontSize: '13px', color: 'var(--text-secondary)' }}>Дней (0 = забрать)</label>
                <input type="number" className="admin-input" value={subDays} onChange={e => setSubDays(e.target.value)} />
                <div className="admin-modal-actions">
                  <button className="btn-admin" onClick={() => setModal(null)}>Отмена</button>
                  <button className="btn-admin" style={{ color: '#60a5fa' }} onClick={() => handleAction(activeUser.id, 'freeze-sub', { product_id: subProduct })}>❄️ Заморозить</button>
                  <button className="btn-admin" style={{ color: '#4ade80' }} onClick={() => handleAction(activeUser.id, 'unfreeze-sub', { product_id: subProduct })}>🔥 Разморозить</button>
                  <button className="btn-primary" onClick={() => handleAction(activeUser.id, 'subscription', { product_id: subProduct, days: subDays })}>Выдать/Забрать</button>
                </div>
              </>
            )}

            {modal === 'media' && (
              <>
                <h3 className="admin-modal-title">Права доступа: {activeUser?.username}</h3>
                <label style={{ display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '8px', color: 'white' }}>
                  <input type="checkbox" checked={mediaActive} onChange={e => setMediaActive(e.target.checked)} />
                  Медиа-партнер (Промокоды)
                </label>
                <label style={{ display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '16px', color: 'white' }}>
                  <input type="checkbox" checked={distributorActive} onChange={e => setDistributorActive(e.target.checked)} />
                  Дистрибьютор (Генерация ключей)
                </label>
                <label style={{ fontSize: '13px', color: 'var(--text-secondary)' }}>Промокод</label>
                <input type="text" className="admin-input" placeholder="Промокод" value={promoCode} onChange={e => setPromoCode(e.target.value)} />
                <label style={{ fontSize: '13px', color: 'var(--text-secondary)' }}>Процент отчислений (%)</label>
                <input type="number" className="admin-input" value={promoPercent} onChange={e => setPromoPercent(e.target.value)} />
                <label style={{ fontSize: '13px', color: 'var(--text-secondary)' }}>Заработок (Рублей)</label>
                <input type="number" className="admin-input" value={promoEarned} onChange={e => setPromoEarned(e.target.value)} />
                <div className="admin-modal-actions">
                  <button className="btn-admin" onClick={() => setModal(null)}>Отмена</button>
                  <button className="btn-primary" onClick={() => handleUpdateUser(activeUser.id, { media_access: mediaActive ? 1 : 0, is_distributor: distributorActive ? 1 : 0, promocode: promoCode, promo_percent: parseInt(promoPercent) || 0, promo_earned: parseInt(promoEarned) || 0 })}>Сохранить</button>
                </div>
              </>
            )}

            {modal === 'distributor_issue' && (
              <>
                <h3 className="admin-modal-title">Выдача ключей дистрибьютору: {activeUser?.username}</h3>
                <label style={{ fontSize: '13px', color: 'var(--text-secondary)' }}>Продукт</label>
                <select className="admin-input" value={keysProduct} onChange={e => setKeysProduct(e.target.value)}>
                  <option value="external">EXTERNAL</option>
                  <option value="trigger">TRIGGER</option>
                  <option value="trigger_silent">TRIGGER + SILENT</option>
                  <option value="ragemp">RAGE:MP</option>
                </select>
                <label style={{ fontSize: '13px', color: 'var(--text-secondary)' }}>Дней подписки на ключ</label>
                <input type="number" className="admin-input" value={keysDays} onChange={e => setKeysDays(e.target.value)} />
                <label style={{ fontSize: '13px', color: 'var(--text-secondary)' }}>Количество ключей</label>
                <input type="number" className="admin-input" value={keysAmount} onChange={e => setKeysAmount(e.target.value)} />
                <div className="admin-modal-actions">
                  <button className="btn-admin" onClick={() => setModal(null)}>Отмена</button>
                  <button className="btn-primary" onClick={handleIssueToDistributor}>Выдать ключи</button>
                </div>
              </>
            )}

            {modal === 'ban' && (
              <>
                <h3 className="admin-modal-title">Заблокировать: {activeUser?.username}</h3>
                <p style={{ fontSize: '14px', color: 'var(--text-secondary)', marginBottom: '16px' }}>
                  Блокировка аккаунта сбросит все подписки и автоматически добавит HWID в черный список.
                </p>
                <input type="text" className="admin-input" placeholder="Причина бана (видима для пользователя)" value={banReason} onChange={e => setBanReason(e.target.value)} />
                <div className="admin-modal-actions">
                  <button className="btn-admin" onClick={() => setModal(null)}>Отмена</button>
                  <button className="btn-danger" onClick={() => handleAction(activeUser.id, 'ban', { reason: banReason })}>Выдать БАН</button>
                </div>
              </>
            )}

            {modal === 'blacklist_add' && (
              <>
                <h3 className="admin-modal-title">Добавить HWID в бан</h3>
                <input type="text" className="admin-input" placeholder="HWID устройства" value={blacklistHwid} onChange={e => setBlacklistHwid(e.target.value)} />
                <input type="text" className="admin-input" placeholder="Причина" value={blacklistReason} onChange={e => setBlacklistReason(e.target.value)} />
                <div className="admin-modal-actions">
                  <button className="btn-admin" onClick={() => setModal(null)}>Отмена</button>
                  <button className="btn-danger" onClick={handleAddBlacklist}>Добавить</button>
                </div>
              </>
            )}

            {modal === 'blacklist_ip_add' && (
              <>
                <h3 className="admin-modal-title">Добавить IP в бан</h3>
                <input type="text" className="admin-input" placeholder="IP адрес (например, 1.2.3.4)" value={blacklistIp} onChange={e => setBlacklistIp(e.target.value)} />
                <input type="text" className="admin-input" placeholder="Причина" value={blacklistIpReason} onChange={e => setBlacklistIpReason(e.target.value)} />
                <div className="admin-modal-actions">
                  <button className="btn-admin" onClick={() => setModal(null)}>Отмена</button>
                  <button className="btn-danger" onClick={handleAddIpBan}>Добавить</button>
                </div>
              </>
            )}

            {modal === 'blacklist_session_add' && (
              <>
                <h3 className="admin-modal-title">Добавить Session UUID в бан</h3>
                <input type="text" className="admin-input" placeholder="Session UUID (полный)" value={blacklistSession} onChange={e => setBlacklistSession(e.target.value)} />
                <input type="text" className="admin-input" placeholder="Причина" value={blacklistSessionReason} onChange={e => setBlacklistSessionReason(e.target.value)} />
                <div className="admin-modal-actions">
                  <button className="btn-admin" onClick={() => setModal(null)}>Отмена</button>
                  <button className="btn-danger" onClick={handleAddSessionBan}>Добавить (+ отозвать сессию)</button>
                </div>
              </>
            )}

            {modal === 'add_version' && (
              <form onSubmit={handleAddVersion}>
                <h3 className="admin-modal-title">Добавить версию Loader</h3>
                <input type="text" className="admin-input" placeholder="Version ID (например, 112)" value={versionId} onChange={e => setVersionId(e.target.value)} required />
                <input type="text" className="admin-input" placeholder="Label (например, Update 2.0)" value={versionLabel} onChange={e => setVersionLabel(e.target.value)} />
                <div className="admin-modal-actions">
                  <button type="button" className="btn-admin" onClick={() => setModal(null)}>Отмена</button>
                  <button type="submit" className="btn-primary">Добавить</button>
                </div>
              </form>
            )}

            {modal === 'keys' && (
              <div style={{ width: '500px' }}>
                <h3 className="admin-modal-title">Генератор ключей</h3>
                <div style={{ display: 'flex', gap: '12px' }}>
                  <select className="admin-input" value={keysProduct} onChange={e => setKeysProduct(e.target.value)}>
                    <option value="external">EXTERNAL</option>
                    <option value="trigger">TRIGGER</option>
                    <option value="trigger_silent">TRIGGER + SILENT</option>
                    <option value="ragemp">RAGE:MP</option>
                  </select>
                  <input type="number" className="admin-input" placeholder="Дней" value={keysDays} onChange={e => setKeysDays(e.target.value)} />
                  <input type="number" className="admin-input" placeholder="Штук" value={keysAmount} onChange={e => setKeysAmount(e.target.value)} />
                </div>
                <button className="btn-primary" onClick={handleGenerateKeys} style={{ width: '100%', marginBottom: '16px' }}>Сгенерировать</button>
                {generatedKeys.length > 0 && (
                  <>
                    <textarea className="admin-input" readOnly rows={6} value={generatedKeys.join('\n')} style={{ fontFamily: 'monospace', resize: 'none' }} />
                    <button className="btn-primary" onClick={() => { navigator.clipboard.writeText(generatedKeys.join('\n')); alert('Все ключи скопированы!'); }}
                      style={{ width: '100%', marginBottom: '16px', background: 'rgba(255,255,255,0.1)', color: 'white', border: '1px solid rgba(255,255,255,0.2)' }}>
                      📋 Скопировать все ключи
                    </button>
                  </>
                )}
                <div className="admin-modal-actions">
                  <button className="btn-admin" onClick={() => { setModal(null); setActiveUser(null); }}>Закрыть</button>
                </div>
              </div>
            )}
            
            {modal === 'give_all' && (
              <div style={{ width: 400 }}>
                <h3 className="admin-modal-title">🎁 Выдать дни всем</h3>
                <label style={{ display: 'block', marginBottom: 8, fontSize: 13, color: '#888' }}>Продукт</label>
                <input className="admin-input" value={giveAllProduct} onChange={e => setGiveAllProduct(e.target.value)} />
                <label style={{ display: 'block', marginBottom: 8, fontSize: 13, color: '#888' }}>Сколько дней выдать?</label>
                <input className="admin-input" type="number" min="1" value={giveAllDays} onChange={e => setGiveAllDays(e.target.value)} />
                <label style={{ display: 'block', marginBottom: 8, fontSize: 13, color: '#888' }}>Кому выдать?</label>
                <select className="admin-input" value={giveAllMode} onChange={e => setGiveAllMode(e.target.value)}>
                  <option value="active_only">Только тем, у кого сейчас активна подписка</option>
                  <option value="all">Абсолютно всем зарегистрированным пользователям</option>
                </select>
                <div className="admin-modal-actions">
                  <button className="btn-admin" onClick={() => setModal(null)}>Отмена</button>
                  <button className="btn-primary" onClick={handleGiveAll}>Выдать</button>
                </div>
              </div>
            )}

          </div>
        </div>
      )}
    </div>
  );
}
