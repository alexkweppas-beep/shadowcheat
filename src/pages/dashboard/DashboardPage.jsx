import React, { useState, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import Header from '../../components/Header';
import { API_BASE } from '../../api.js';
import './DashboardPage.css';

export default function DashboardPage() {
  const navigate = useNavigate();
  const [user, setUser] = useState(null);
  const [hasActiveSub, setHasActiveSub] = useState(false);
  const [isTopUpOpen, setIsTopUpOpen] = useState(false);
  const [isPasswordModalOpen, setIsPasswordModalOpen] = useState(false);
  const [isFreezeModalOpen, setIsFreezeModalOpen] = useState(false);
  const [isActivateModalOpen, setIsActivateModalOpen] = useState(false);
  const [isDownloadModalOpen, setIsDownloadModalOpen] = useState(false);
  const [selectedSubToFreeze, setSelectedSubToFreeze] = useState(null);

  useEffect(() => {
    const token = localStorage.getItem('token');
    if (!token) {
      navigate('/');
      return;
    }
    fetch(`${API_BASE}/api/user/me`, {
      headers: { 'Authorization': `Bearer ${token}` }
    })
      .then(res => res.json())
      .then(data => {
        if (data.error) {
          localStorage.removeItem('token');
          navigate('/');
        } else {
          setUser(data);
          setHasActiveSub(data.subscriptions && data.subscriptions.length > 0);
        }
      })
      .catch(console.error);
  }, [navigate]);

  const [oldPassword, setOldPassword] = useState('');
  const [newPassword, setNewPassword] = useState('');
  const [newPasswordConfirm, setNewPasswordConfirm] = useState('');
  const [topupAmount, setTopupAmount] = useState('');
  const [topupMethod, setTopupMethod] = useState('СБП');
  const [activationKey, setActivationKey] = useState('');
  const [loaderCustomName, setLoaderCustomName] = useState('');
  const [useCustomLoaderName, setUseCustomLoaderName] = useState(false);

  const handleTopup = async () => {
    const amount = parseInt(topupAmount);
    if (isNaN(amount) || amount < 100) return alert('Мин. 100 рублей');
    try {
      const res = await fetch(`${API_BASE}/api/user/me/topup`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'Authorization': `Bearer ${localStorage.getItem('token')}` },
        body: JSON.stringify({ amount, method: topupMethod })
      });
      const data = await res.json();
      if (data.ok && data.url) {
        // Открываем страницу оплаты
        window.open(data.url, '_blank');
        setIsTopUpOpen(false);
        // Для СБП — polling статуса
        if (topupMethod === 'СБП' && data.invoice_id) {
          const invoiceId = data.invoice_id;
          let attempts = 0;
          const poll = setInterval(async () => {
            attempts++;
            try {
              const sr = await fetch(`${API_BASE}/api/payments/platega/status/${invoiceId}`, {
                headers: { 'Authorization': `Bearer ${localStorage.getItem('token')}` }
              });
              const sd = await sr.json();
              if (sd.status === 'completed') {
                clearInterval(poll);
                setUser(prev => ({ ...prev, balance: sd.balance }));
                alert(`✅ Баланс пополнен! Новый баланс: ${sd.balance} ₽`);
              }
            } catch (e) { }
            if (attempts >= 60) clearInterval(poll); // 5 минут
          }, 5000);
        }
      } else {
        alert(data.error || 'Ошибка');
      }
    } catch (e) { alert('Ошибка'); }
  };

  const handlePasswordChange = async () => {
    if (newPassword !== newPasswordConfirm) return alert('Пароли не совпадают');
    try {
      const res = await fetch(`${API_BASE}/api/user/me/password`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'Authorization': `Bearer ${localStorage.getItem('token')}` },
        body: JSON.stringify({ oldPassword, newPassword })
      });
      const data = await res.json();
      if (data.ok) {
        alert('Пароль успешно изменён');
        setIsPasswordModalOpen(false);
      } else alert(data.error);
    } catch (e) { alert('Ошибка'); }
  };

  const handleHwidReset = async () => {
    if (!window.confirm('Сброс HWID стоит 100 рублей. Продолжить?')) return;
    try {
      const res = await fetch(`${API_BASE}/api/user/me/hwid-reset`, {
        method: 'POST',
        headers: { 'Authorization': `Bearer ${localStorage.getItem('token')}` }
      });
      const data = await res.json();
      if (data.ok) {
        alert('HWID успешно сброшен!');
        window.location.reload();
      } else alert(data.error);
    } catch (e) { alert('Ошибка'); }
  };

  const handleFreeze = async () => {
    if (!selectedSubToFreeze) return alert('Выберите подписку');
    try {
      const res = await fetch(`${API_BASE}/api/user/me/freeze`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'Authorization': `Bearer ${localStorage.getItem('token')}` },
        body: JSON.stringify({ product_id: selectedSubToFreeze })
      });
      const data = await res.json();
      if (data.ok) {
        alert('Подписка заморожена');
        setIsFreezeModalOpen(false);
        window.location.reload();
      } else alert(data.error);
    } catch (e) { alert('Ошибка'); }
  };

  const handleUnfreeze = async () => {
    if (!selectedSubToFreeze) return alert('Выберите подписку');
    try {
      const res = await fetch(`${API_BASE}/api/user/me/unfreeze`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'Authorization': `Bearer ${localStorage.getItem('token')}` },
        body: JSON.stringify({ product_id: selectedSubToFreeze })
      });
      const data = await res.json();
      if (data.ok) {
        alert('Подписка разморожена');
        setIsFreezeModalOpen(false);
        window.location.reload();
      } else alert(data.error);
    } catch (e) { alert('Ошибка'); }
  };

  const handleActivate = async () => {
    if (!activationKey) return alert('Введите ключ');
    try {
      const res = await fetch(`${API_BASE}/api/auth/activate`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'Authorization': `Bearer ${localStorage.getItem('token')}` },
        body: JSON.stringify({ key: activationKey })
      });
      const data = await res.json();
      if (data.ok) {
        alert('Ключ успешно активирован!');
        setIsActivateModalOpen(false);
        window.location.reload();
      } else alert(data.error);
    } catch (e) { alert('Ошибка'); }
  };

  const handleDownloadLoader = async () => {
    try {
      // Шаг 1: получить одноразовый токен (мгновенно — просто запись в памяти сервера)
      const res = await fetch(`${API_BASE}/api/loader/request_download`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'Authorization': `Bearer ${localStorage.getItem('token')}` },
        body: JSON.stringify({})
      });
      if (!res.ok) {
        const data = await res.json();
        return alert(data.error || 'Ошибка скачивания');
      }
      const { token } = await res.json();

      // Шаг 2: Нативное скачивание браузером — стартует моментально, виден прогресс
      let finalName = 'VoiceChanger64(2.02).exe';
      if (useCustomLoaderName && loaderCustomName.trim() !== '') {
        finalName = loaderCustomName.trim();
        if (!finalName.toLowerCase().endsWith('.exe')) finalName += '.exe';
      }

      const a = document.createElement('a');
      a.href = `${API_BASE}/api/loader/download_loader/${token}`;
      a.download = finalName;
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
    } catch (e) {
      alert('Ошибка сервера');
    }
  };

  return (
    <div className="dashboard-page">
      <Header />

      <div className="container dashboard-container">

        {/* Top Section (From Screenshot 2) */}
        <div className="dash-top-section">
          {/* Subscriptions Card */}
          <div className="dash-card subs-card">
            <div className="subs-header">
              <span className="subs-label">
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><rect x="3" y="4" width="18" height="18" rx="2" ry="2"></rect><line x1="16" y1="2" x2="16" y2="6"></line><line x1="8" y1="2" x2="8" y2="6"></line><line x1="3" y1="10" x2="21" y2="10"></line></svg>
                Текущие подписки
              </span>
            </div>
            <div className="subs-list">
              {user && user.subscriptions && user.subscriptions.length > 0 ? (
                (() => {
                  // Группируем по product_id
                  const subMap = {};
                  user.subscriptions.forEach(s => {
                    const pid = (s.product_id || '').toLowerCase();
                    if (!subMap[pid]) subMap[pid] = { days_left: 0, frozen: false };
                    subMap[pid].days_left += s.days_left || 0;
                    if (s.frozen_at || s.is_frozen) subMap[pid].frozen = true;
                  });
                  const LABELS = {
                    external: 'External',
                    trigger: 'Trigger',
                    trigger_silent: 'Trigger + Silent',
                    silent: 'Silent Aim'
                  };
                  return Object.entries(subMap).map(([pid, info]) => (
                    <div className="sub-item" key={pid}>
                      <span className="sub-name">• {LABELS[pid] || pid}</span>
                      {info.frozen ? (
                        <span className="sub-status" style={{ color: '#60a5fa' }}>❄ Заморожено ({info.days_left} ч.)</span>
                      ) : (
                        <span className="sub-status" style={{ color: '#4ade80' }}>Активно ({info.days_left} ч.)</span>
                      )}
                    </div>
                  ));
                })()
              ) : (
                <div className="sub-item">
                  <span className="sub-name" style={{ color: 'rgba(255,255,255,0.3)' }}>Нет активных подписок</span>
                </div>
              )}
            </div>
            <div className="subs-action">
              <button
                className="dash-btn btn-glass w-full"
                disabled={!hasActiveSub || (user && user.subscriptions && user.subscriptions.some(s => s.frozen_at))}
                style={{ opacity: (hasActiveSub && !(user && user.subscriptions && user.subscriptions.some(s => s.frozen_at))) ? 1 : 0.5, cursor: (hasActiveSub && !(user && user.subscriptions && user.subscriptions.some(s => s.frozen_at))) ? 'pointer' : 'not-allowed' }}
                onClick={() => setIsDownloadModalOpen(true)}
              >
                Скачать лоадер
              </button>
            </div>
          </div>
        </div>

        {/* Row 2: User Info */}
        <div className="dash-row dash-row-2">
          <div className="dash-card info-card">
            <div className="info-label">
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"></path><circle cx="12" cy="7" r="4"></circle></svg>
              Имя пользователя
            </div>
            <div className="info-val">{user ? user.username : 'Загрузка...'}</div>
          </div>
          <div className="dash-card info-card">
            <div className="info-label">
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M4 4h16c1.1 0 2 .9 2 2v12c0 1.1-.9 2-2 2H4c-1.1 0-2-.9-2-2V6c0-1.1.9-2 2-2z"></path><polyline points="22,6 12,13 2,6"></polyline></svg>
              Почта
            </div>
            <div className="info-val">{user ? (user.email || 'Не указана') : 'Загрузка...'}</div>
          </div>
        </div>

        {/* Row 3: Account Stats */}
        <div className="dash-row dash-row-3">
          <div className="dash-card info-card">
            <div className="info-label">
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><circle cx="12" cy="12" r="10"></circle><path d="M12 8v8M8 12h8"></path></svg>
              Баланс аккаунта
            </div>
            <div className="info-val">{user ? user.balance : 0} рублей</div>
          </div>
          <div className="dash-card info-card">
            <div className="info-label">
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><rect x="3" y="4" width="18" height="18" rx="2" ry="2"></rect><line x1="16" y1="2" x2="16" y2="6"></line><line x1="8" y1="2" x2="8" y2="6"></line><line x1="3" y1="10" x2="21" y2="10"></line></svg>
              Дата регистрации
            </div>
            <div className="info-val">{user ? new Date(user.created_at).toLocaleString('ru-RU', { day: 'numeric', month: 'short', year: 'numeric', hour: '2-digit', minute: '2-digit' }) : '...'}</div>
          </div>
          <div className="dash-card info-card">
            <div className="info-label">
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><rect x="2" y="3" width="20" height="14" rx="2" ry="2"></rect><line x1="8" y1="21" x2="16" y2="21"></line><line x1="12" y1="17" x2="12" y2="21"></line></svg>
              HWID
            </div>
            <div className="info-val">{user ? (user.hwid ? 'Привязан' : 'Не привязан') : '...'}</div>
          </div>
        </div>

        {/* Row 4: Actions */}
        <div className="dash-row dash-row-2">
          <div className="dash-card action-card-horizontal">
            <h3 className="action-title mb-0">Сменить пароль</h3>
            <button className="dash-btn btn-dark" onClick={() => setIsPasswordModalOpen(true)}>Изменить пароль</button>
          </div>
          <div className="dash-card action-card-horizontal">
            <h3 className="action-title mb-0">Пополнить баланс</h3>
            <button className="dash-btn btn-dark" onClick={() => setIsTopUpOpen(true)}>Пополнить баланс</button>
          </div>
        </div>

        {/* Row 5: Quick Actions */}
        <div className="dash-row dash-row-3">
          <div className="dash-card quick-card">
            <span className="quick-title">Сбросить HWID</span>
            <button className="dash-btn btn-dark-sm" onClick={handleHwidReset}>100р</button>
          </div>
          <div className="dash-card quick-card">
            <span className="quick-title">Заморозка подписки</span>
            <button className="dash-btn btn-dark-sm" onClick={() => setIsFreezeModalOpen(true)}>Заморозить/Разморозить</button>
          </div>
          <div className="dash-card quick-card">
            <span className="quick-title">Активация ключа</span>
            <button className="dash-btn btn-dark-sm" onClick={() => setIsActivateModalOpen(true)}>Активировать</button>
          </div>
        </div>

        {/* Links to special pages */}
        <div style={{ marginTop: '40px', display: 'flex', justifyContent: 'center', gap: '16px', flexWrap: 'wrap' }}>
          {user && user.media_access ? (
            <button className="dash-btn btn-white" onClick={() => navigate('/media')}>
              Кабинет Медиа-партнера
            </button>
          ) : null}

          {user && user.is_distributor ? (
            <button className="dash-btn btn-white" onClick={() => navigate('/distributor')}>
              Кабинет Дистрибьютора
            </button>
          ) : null}

          {localStorage.getItem('adminToken') && (
            <button className="dash-btn btn-white" style={{ background: 'rgba(239, 68, 68, 0.1)', color: '#ef4444', border: '1px solid rgba(239, 68, 68, 0.2)' }} onClick={() => navigate('/admin444')}>
              Перейти в Админ-Панель
            </button>
          )}
        </div>

      </div>

      {/* Topup Modal */}
      {isTopUpOpen && (
        <div className="dash-modal-overlay">
          <div className="dash-modal">
            <div className="dash-modal-header">
              <h3 className="dash-modal-title">Пополнение баланса</h3>
              <button className="dash-modal-close" onClick={() => setIsTopUpOpen(false)}>
                <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><line x1="18" y1="6" x2="6" y2="18"></line><line x1="6" y1="6" x2="18" y2="18"></line></svg>
              </button>
            </div>

            <div className="dash-modal-body">
              <div className="dash-modal-label">Платёжные системы</div>
              <div className="dash-payment-systems">
                <button className={`dash-btn dash-ps-btn ${topupMethod === 'СБП' ? 'active' : ''}`} onClick={() => setTopupMethod('СБП')}>СБП</button>
                <button className={`dash-btn dash-ps-btn ${topupMethod === 'CRYPTOBOT' ? 'active' : ''}`} onClick={() => setTopupMethod('CRYPTOBOT')}>CRYPTOBOT</button>
              </div>

              <input type="number" className="dash-input mt-4 w-full" placeholder="Сумма к пополнению (Мин. 100 рублей)" value={topupAmount} onChange={e => setTopupAmount(e.target.value)} />
            </div>

            <div className="dash-modal-footer">
              <button className="dash-btn dash-btn-cancel" onClick={() => setIsTopUpOpen(false)}>Отмена</button>
              <button className="dash-btn btn-glass" onClick={handleTopup}>Оплатить</button>
            </div>
          </div>
        </div>
      )}

      {/* Change Password Modal */}
      {isPasswordModalOpen && (
        <div className="dash-modal-overlay">
          <div className="dash-modal">
            <div className="dash-modal-header">
              <h3 className="dash-modal-title">Изменить пароль</h3>
              <button className="dash-modal-close" onClick={() => setIsPasswordModalOpen(false)}>
                <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><line x1="18" y1="6" x2="6" y2="18"></line><line x1="6" y1="6" x2="18" y2="18"></line></svg>
              </button>
            </div>

            <div className="dash-modal-body">
              <div className="dash-input-icon-wrapper">
                <svg className="input-icon-left" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M21 2l-2 2m-7.61 7.61a5.5 5.5 0 1 1-7.778 7.778 5.5 5.5 0 0 1 7.777-7.777zm0 0L15.5 7.5m0 0l3 3L22 7l-3-3m-3.5 3.5L19 4"></path></svg>
                <input type="password" className="dash-input w-full" placeholder="Старый пароль" value={oldPassword} onChange={e => setOldPassword(e.target.value)} />
                <svg className="input-icon-right" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"></path><circle cx="12" cy="12" r="3"></circle></svg>
              </div>

              <div className="dash-input-icon-wrapper mt-3">
                <svg className="input-icon-left" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M21 2l-2 2m-7.61 7.61a5.5 5.5 0 1 1-7.778 7.778 5.5 5.5 0 0 1 7.777-7.777zm0 0L15.5 7.5m0 0l3 3L22 7l-3-3m-3.5 3.5L19 4"></path></svg>
                <input type="password" className="dash-input w-full" placeholder="Новый пароль" value={newPassword} onChange={e => setNewPassword(e.target.value)} />
                <svg className="input-icon-right" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"></path><circle cx="12" cy="12" r="3"></circle></svg>
              </div>

              <div className="dash-input-icon-wrapper mt-3">
                <svg className="input-icon-left" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M21 2l-2 2m-7.61 7.61a5.5 5.5 0 1 1-7.778 7.778 5.5 5.5 0 0 1 7.777-7.777zm0 0L15.5 7.5m0 0l3 3L22 7l-3-3m-3.5 3.5L19 4"></path></svg>
                <input type="password" className="dash-input w-full" placeholder="Повторите новый пароль" value={newPasswordConfirm} onChange={e => setNewPasswordConfirm(e.target.value)} />
                <svg className="input-icon-right" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"></path><circle cx="12" cy="12" r="3"></circle></svg>
              </div>
            </div>

            <div className="dash-modal-footer">
              <button className="dash-btn dash-btn-cancel" onClick={() => setIsPasswordModalOpen(false)}>Отмена</button>
              <button className="dash-btn btn-glass" onClick={handlePasswordChange}>Подтвердить</button>
            </div>
          </div>
        </div>
      )}

      {/* Freeze Subscription Modal */}
      {isFreezeModalOpen && (
        <div className="dash-modal-overlay">
          <div className="dash-modal">
            <div className="dash-modal-header" style={{ marginBottom: '16px' }}>
              <div style={{ display: 'flex', alignItems: 'flex-start', gap: '12px' }}>
                <div>
                  <h3 className="dash-modal-title">Заморозка подписки</h3>
                  <p style={{ color: 'rgba(255,255,255,0.5)', fontSize: '13px', marginTop: '8px', lineHeight: '1.4' }}>
                    Вы действительно хотите заморозить свою<br />подписку? Подписку можно заморозить раз<br />в неделю.
                  </p>
                </div>
              </div>
              <button className="dash-modal-close" onClick={() => setIsFreezeModalOpen(false)} style={{ alignSelf: 'flex-start' }}>
                <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><line x1="18" y1="6" x2="6" y2="18"></line><line x1="6" y1="6" x2="18" y2="18"></line></svg>
              </button>
            </div>

            <div className="dash-modal-body">
              <div className="dash-freeze-list">
                {user && user.subscriptions && user.subscriptions.length > 0 ? (
                  (() => {
                    const subMap = {};
                    user.subscriptions.forEach(s => {
                      const pid = (s.product_id || '').toLowerCase();
                      if (!subMap[pid]) subMap[pid] = { days_left: 0, frozen: false };
                      subMap[pid].days_left += s.days_left || 0;
                      if (s.frozen_at || s.is_frozen) subMap[pid].frozen = true;
                    });
                    const LABELS = {
                      external: 'External',
                      trigger: 'Trigger',
                      trigger_silent: 'Trigger + Silent',
                      silent: 'Silent Aim'
                    };
                    return Object.entries(subMap).map(([pid, info]) => (
                      <div
                        key={pid}
                        className={`dash-freeze-item ${selectedSubToFreeze === pid ? 'active' : ''}`}
                        onClick={() => setSelectedSubToFreeze(pid)}
                      >
                        <span className="sub-name">{LABELS[pid] || pid}</span>
                        {info.frozen ? (
                          <span className="sub-status" style={{ color: '#60a5fa' }}>❄ Заморожено ({info.days_left} ч.)</span>
                        ) : (
                          <span className="sub-status" style={{ color: '#4ade80' }}>Активно ({info.days_left} ч.)</span>
                        )}
                      </div>
                    ));
                  })()
                ) : (
                  <div className="dash-freeze-item" style={{ opacity: 0.4, pointerEvents: 'none' }}>
                    <span className="sub-name">Нет подписок</span>
                  </div>
                )}
              </div>
            </div>

            <div className="dash-modal-footer">
              <button className="dash-btn dash-btn-cancel" onClick={() => setIsFreezeModalOpen(false)}>Отмена</button>
              {user && user.subscriptions && user.subscriptions.find(s => s.product_id === selectedSubToFreeze)?.frozen_at ? (
                <button className="dash-btn btn-glass" disabled={!selectedSubToFreeze} onClick={handleUnfreeze}>Разморозить</button>
              ) : (
                <button className="dash-btn btn-glass" disabled={!selectedSubToFreeze} onClick={handleFreeze}>Заморозить</button>
              )}
            </div>
          </div>
        </div>
      )}

      {/* Activate Key Modal */}
      {isActivateModalOpen && (
        <div className="dash-modal-overlay">
          <div className="dash-modal">
            <div className="dash-modal-header">
              <h3 className="dash-modal-title">Активация ключа</h3>
              <button className="dash-modal-close" onClick={() => setIsActivateModalOpen(false)}>
                <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><line x1="18" y1="6" x2="6" y2="18"></line><line x1="6" y1="6" x2="18" y2="18"></line></svg>
              </button>
            </div>

            <div className="dash-modal-body">
              <div className="dash-input-icon-wrapper">
                <svg className="input-icon-left" width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M21 2l-2 2m-7.61 7.61a5.5 5.5 0 1 1-7.778 7.778 5.5 5.5 0 0 1 7.777-7.777zm0 0L15.5 7.5m0 0l3 3L22 7l-3-3m-3.5 3.5L19 4"></path></svg>
                <input type="text" className="dash-input w-full" placeholder="Введите ключ" value={activationKey} onChange={e => setActivationKey(e.target.value)} />
              </div>
            </div>

            <div className="dash-modal-footer">
              <button className="dash-btn dash-btn-cancel" onClick={() => setIsActivateModalOpen(false)}>Отмена</button>
              <button className="dash-btn btn-glass" onClick={handleActivate}>Подтвердить</button>
            </div>
          </div>
        </div>
      )}

      {/* Download Loader Modal */}
      {isDownloadModalOpen && (
        <div className="dash-modal-overlay">
          <div className="dash-modal" style={{ maxWidth: '520px' }}>
            <div className="dash-modal-header">
              <h3 className="dash-modal-title">Скачать лоадер</h3>
              <button className="dash-modal-close" onClick={() => setIsDownloadModalOpen(false)}>
                <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><line x1="18" y1="6" x2="6" y2="18"></line><line x1="6" y1="6" x2="18" y2="18"></line></svg>
              </button>
            </div>

            <div className="dash-modal-body">
              <ul className="download-steps">
                <li className="download-step"><div className="step-num">1</div> Запуск лоадера <b>до игры</b></li>
                <li className="download-step"><div className="step-num">2</div> Нажать <b>Launch</b></li>
                <li className="download-step"><div className="step-num">3</div> Дождаться маппинга драйвера</li>
                <li className="download-step"><div className="step-num">4</div> Зайти в игру и дождаться входа на сервер</li>
                <li className="download-step"><div className="step-num">5</div> Выставить настройки и нажать <b>Enable</b></li>
              </ul>

              <div className="hotkey-box" style={{ cursor: 'text' }} onClick={(e) => e.currentTarget.querySelector('input').focus()}>
                <input 
                  type="text" 
                  placeholder="Введите желаемое название лоадера" 
                  className="hotkey-label"
                  style={{ background: 'transparent', border: 'none', outline: 'none', width: '100%', padding: 0 }}
                  value={loaderCustomName}
                  onChange={(e) => setLoaderCustomName(e.target.value)}
                />
                <div className="hotkey-keys" onClick={(e) => { e.stopPropagation(); setUseCustomLoaderName(!useCustomLoaderName); }} style={{ cursor: 'pointer' }}>
                  <div style={{ width: '26px', height: '26px', background: useCustomLoaderName ? 'rgba(255, 255, 255, 0.3)' : 'rgba(255, 255, 255, 0.1)', borderRadius: '4px', display: 'flex', alignItems: 'center', justifyContent: 'center', transition: 'background 0.2s' }}>
                    {useCustomLoaderName && (
                      <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#fff" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>
                    )}
                  </div>
                </div>
              </div>

              <div className="warning-box">
                <div className="warning-icon">
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"></path><line x1="12" y1="9" x2="12" y2="13"></line><line x1="12" y1="17" x2="12.01" y2="17"></line></svg>
                </div>
                <div>Обязательно открывайте лоадер от имени администратора и отключите все Антивирусы, Windows Defender и Изоляцию Ядра</div>
              </div>

              <div className="warning-box">
                <div className="warning-icon">
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"></path><line x1="12" y1="9" x2="12" y2="13"></line><line x1="12" y1="17" x2="12.01" y2="17"></line></svg>
                </div>
                <div>После использования продукта <b>перезагрузите ПК</b> перед запуском игр с античитом <b>EAC/BE</b> (Fortnite, Rust, PUBG и др.)</div>
              </div>

              <div className="warning-box">
                <div className="warning-icon">
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"></path><line x1="12" y1="9" x2="12" y2="13"></line><line x1="12" y1="17" x2="12.01" y2="17"></line></svg>
                </div>
                <div>external чит работает только в режиме <b>окно на весь экран</b>, в полноэкранном режиме работать не будет!</div>
              </div>

              <div className="warning-box">
                <div className="warning-icon">
                  <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M10.29 3.86L1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0z"></path><line x1="12" y1="9" x2="12" y2="13"></line><line x1="12" y1="17" x2="12.01" y2="17"></line></svg>
                </div>
                <div>FACEIT Anti-Cheat иногда может блокировать загрузку драйвера, обязательно удалите его перед игрой с нашим продуктом.</div>
              </div>
            </div>

            <div className="dash-modal-footer">
              <button className="dash-btn dash-btn-cancel" onClick={() => setIsDownloadModalOpen(false)}>Отмена</button>
              <button className="dash-btn dash-btn-white" onClick={() => { setIsDownloadModalOpen(false); handleDownloadLoader(); }}>Скачать лоадер</button>
            </div>
          </div>
        </div>
      )}

    </div>
  );
}
